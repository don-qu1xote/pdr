#!/usr/bin/env python3
"""Проверка направления зависимостей между слоями по директивам #include.

Слой файла определяется каталогом: ближайший к файлу каталог с именем core,
application или infrastructure. Запрещено:

    core/         -> userver, pqxx/libpq, application/, infrastructure/
    application/  -> userver, pqxx/libpq, infrastructure/
    infrastructure/ -> ничего не запрещено, это внешний слой

Там же запрещено прямое обращение к системному времени: <ctime>, <time.h>,
system_clock::now() и подобное. Часы приходят портом application::ports::Clock,
иначе тест расписания зависит от секунды, в которую его запустили.

Нарушение печатается как <файл>:<строка>: <причина> и даёт код возврата 1.

Разбираются именно директивы препроцессора, а не имена файлов: комментарии и
строковые литералы вырезаются с сохранением нумерации строк, после чего
директива ищется с начала строки. Совпадение по подстроке «userver» где-нибудь
в тексте нарушением не считается.

Запуск:
    python3 scripts/check_layers.py            # весь репозиторий
    python3 scripts/check_layers.py libs/      # только часть дерева
    python3 scripts/check_layers.py --selftest # проверить саму проверку
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Iterator, Sequence

SOURCE_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".ipp", ".c", ".cc", ".cpp", ".cxx"})
SKIPPED_DIRS = frozenset({"build", "out", "node_modules", "_deps", "third_party"})
LAYERS = ("core", "application", "infrastructure")

PQ_ROOTS = frozenset({"pqxx", "libpq"})
PQ_HEADERS = frozenset({"libpq-fe.h", "libpq-events.h", "postgres_fe.h"})

TIME_HEADERS = frozenset({"ctime", "time.h", "sys/time.h", "sys/times.h"})

# Прямое обращение к системному времени. Ищется по тексту без комментариев и
# литералов: имя типа std::chrono::system_clock само по себе не запрещено,
# запрещён вызов «который час».
CLOCK_CALLS = (
    "system_clock::now",
    "steady_clock::now",
    "high_resolution_clock::now",
    "gettimeofday",
    "clock_gettime",
    "time(nullptr)",
    "time(NULL)",
    "::time(",
)
CLOCK_FORBIDDEN_IN = frozenset({"core", "application"})

IDENTIFIER_SYMBOLS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
RAW_STRING_PREFIXES = frozenset("LuU8")


def strip_comments(text: str, strip_literals: bool = False) -> str:
    """Убрать комментарии, сохранив номера строк.

    strip_literals=True дополнительно вычищает строковые и символьные литералы:
    строка "system_clock::now()" в коде — текст, а не обращение ко времени.
    Для разбора #include литералы вычищать нельзя — в них лежит путь.
    """
    out: list[str] = []
    index = 0
    size = len(text)
    while index < size:
        symbol = text[index]
        following = text[index + 1] if index + 1 < size else ""

        if symbol == "/" and following == "/":
            while index < size and text[index] != "\n":
                out.append(" ")
                index += 1
            continue

        if symbol == "/" and following == "*":
            out.append("  ")
            index += 2
            while index + 1 < size and not (text[index] == "*" and text[index + 1] == "/"):
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
            if index + 1 < size:
                out.append("  ")
                index += 2
            else:
                index = size
            continue

        if symbol == "R" and following == '"' and _is_literal_start(text, index):
            index = _skip_raw_string(text, index, out)
            continue

        if strip_literals and symbol == '"':
            index = _skip_quoted(text, index, out, '"')
            continue

        if strip_literals and symbol == "'" and _is_literal_start(text, index):
            index = _skip_quoted(text, index, out, "'")
            continue

        out.append(symbol)
        index += 1

    return "".join(out)


def _is_literal_start(text: str, index: int) -> bool:
    """Литерал ли это.

    Отличает R"(...)" от хвоста идентификатора вроде kMyR и символьный литерал
    '9' от разделителя разрядов в 1'000'000. Префиксы L, u, U, u8 учитываются.
    """
    if index == 0:
        return True
    previous = text[index - 1]
    if previous in RAW_STRING_PREFIXES:
        return index < 2 or text[index - 2] not in IDENTIFIER_SYMBOLS
    return previous not in IDENTIFIER_SYMBOLS


def _skip_quoted(text: str, index: int, out: list[str], quote: str) -> int:
    """Заменить "..." или '...' пробелами; вернуть позицию за литералом."""
    out.append(" ")
    position = index + 1
    while position < len(text) and text[position] != "\n":
        symbol = text[position]
        out.append(" ")
        position += 1
        if symbol == "\\" and position < len(text) and text[position] != "\n":
            out.append(" ")
            position += 1
            continue
        if symbol == quote:
            return position
    return position


def _skip_raw_string(text: str, index: int, out: list[str]) -> int:
    """Заменить R"delim( ... )delim" пробелами; вернуть позицию за литералом."""
    opening = text.find("(", index)
    if opening == -1:
        out.append(text[index])
        return index + 1

    terminator = ")" + text[index + 2 : opening] + '"'
    closing = text.find(terminator, opening)
    end = len(text) if closing == -1 else closing + len(terminator)
    for symbol in text[index:end]:
        out.append("\n" if symbol == "\n" else " ")
    return end


def includes(text: str) -> Iterator[tuple[int, str, str]]:
    """(номер строки, путь из директивы, сама директива) для каждого #include."""
    for number, line in enumerate(strip_comments(text).splitlines(), start=1):
        rest = line.lstrip()
        if not rest.startswith("#"):
            continue
        rest = rest[1:].lstrip()
        if not rest.startswith("include"):
            continue
        rest = rest[len("include") :].lstrip()
        if not rest or rest[0] not in '<"':
            continue
        closing = ">" if rest[0] == "<" else '"'
        end = rest.find(closing, 1)
        if end == -1:
            continue
        yield number, rest[1:end], f"#include {rest[: end + 1]}"


def clock_calls(text: str) -> Iterator[tuple[int, str]]:
    """(номер строки, вызов) для каждого прямого обращения к системному времени."""
    without_literals = strip_comments(text, strip_literals=True)
    for number, line in enumerate(without_literals.splitlines(), start=1):
        for call in CLOCK_CALLS:
            if call in line:
                yield number, call
                break


def layer_of(path: Path) -> str | None:
    for part in reversed(path.parts[:-1]):
        if part in LAYERS:
            return part
    return None


def _uses_userver(parts: Sequence[str], include: str) -> bool:
    return bool(parts) and parts[0] == "userver"


def _uses_postgres_driver(parts: Sequence[str], include: str) -> bool:
    return (bool(parts) and parts[0] in PQ_ROOTS) or include in PQ_HEADERS


def _uses_system_time(parts: Sequence[str], include: str) -> bool:
    return include in TIME_HEADERS


def _uses_layer(layer: str) -> Callable[[Sequence[str], str], bool]:
    def predicate(parts: Sequence[str], include: str) -> bool:
        return layer in parts[:-1]

    return predicate


Rule = tuple[str, Callable[[Sequence[str], str], bool]]

FORBIDDEN: dict[str, tuple[Rule, ...]] = {
    "core": (
        ("userver", _uses_userver),
        ("pqxx/libpq", _uses_postgres_driver),
        ("системное время", _uses_system_time),
        ("слой application", _uses_layer("application")),
        ("слой infrastructure", _uses_layer("infrastructure")),
    ),
    "application": (
        ("userver", _uses_userver),
        ("pqxx/libpq", _uses_postgres_driver),
        ("системное время", _uses_system_time),
        ("слой infrastructure", _uses_layer("infrastructure")),
    ),
    "infrastructure": (),
}


def source_files(paths: Iterable[Path]) -> Iterator[Path]:
    for path in paths:
        if path.is_file():
            if path.suffix in SOURCE_SUFFIXES:
                yield path
            continue
        for candidate in sorted(path.rglob("*")):
            if candidate.suffix not in SOURCE_SUFFIXES or not candidate.is_file():
                continue
            if any(part in SKIPPED_DIRS or part.startswith(".") for part in candidate.parts):
                continue
            yield candidate


def check_file(path: Path, display: Path) -> list[str]:
    layer = layer_of(path)
    if layer is None:
        return []
    rules = FORBIDDEN[layer]
    if not rules:
        return []

    violations: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    for number, include, directive in includes(text):
        parts = PurePosixPath(include).parts
        for name, predicate in rules:
            if predicate(parts, include):
                violations.append(
                    f"{display}:{number}: слой {layer} не может включать {name}: {directive}"
                )

    if layer in CLOCK_FORBIDDEN_IN:
        for number, call in clock_calls(text):
            violations.append(
                f"{display}:{number}: слой {layer} не может обращаться к системному времени "
                f"({call}): часы приходят портом application::ports::Clock"
            )
    return violations


def check(paths: Sequence[Path], root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    checked = 0
    for path in source_files(paths):
        checked += 1
        try:
            display = path.relative_to(root)
        except ValueError:
            display = path
        violations.extend(check_file(path, display))
    return violations, checked


SELFTEST_FILES = {
    "src/core/good.cpp": (
        '#include <string>\n'
        '// #include <userver/engine/sleep.hpp> — в комментарии, это не зависимость\n'
        '/* #include "infrastructure/postgres.hpp" */\n'
        'const char* kSample = R"(\n#include <pqxx/pqxx>\n)";\n'
        '#include "core/money.hpp"\n'
    ),
    "src/core/bad.cpp": (
        '#include <string>\n'
        '#include <userver/engine/sleep.hpp>\n'
        '#  include <pqxx/pqxx>\n'
        '#include "infrastructure/postgres_tariff_repository.hpp"\n'
        '#include "application/quote_lesson_package.hpp"\n'
    ),
    "src/application/bad.cpp": (
        '#include <userver/components/component_base.hpp>\n'
        '#include "core/tariff.hpp"\n'
        '#include <ctime>\n'
    ),
    "src/core/clock.cpp": (
        '#include <chrono>\n'
        'const char* kSample = "std::chrono::system_clock::now()";\n'
        'const int kMillion = 1\'000\'000;\n'
        'const char kQuote = \'"\';\n'
        'auto Sleep() { return std::chrono::milliseconds{1}; }\n'
        'auto Broken() { return std::chrono::system_clock::now(); }\n'
        'auto AlsoBroken() { return std::time(nullptr); }\n'
    ),
    "src/infrastructure/good.cpp": (
        '#include <userver/storages/postgres/cluster.hpp>\n'
        '#include <ctime>\n'
        '#include "application/ports/tariff_repository.hpp"\n'
        'auto Now() { return std::chrono::system_clock::now(); }\n'
    ),
}

SELFTEST_EXPECTED = {
    ("src/core/bad.cpp", 2),
    ("src/core/bad.cpp", 3),
    ("src/core/bad.cpp", 4),
    ("src/core/bad.cpp", 5),
    ("src/application/bad.cpp", 1),
    ("src/application/bad.cpp", 3),
    ("src/core/clock.cpp", 6),
    ("src/core/clock.cpp", 7),
}


def selftest() -> int:
    """Отрицательный случай: проверка обязана падать и называть файл и строку."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, checked = check([root], root)

        found = set()
        for line in violations:
            location, _, _ = line.partition(": ")
            name, _, number = location.rpartition(":")
            found.add((name.replace("\\", "/"), int(number)))

        if checked != len(SELFTEST_FILES):
            print(f"самопроверка: проверено {checked} файлов из {len(SELFTEST_FILES)}",
                  file=sys.stderr)
            return 1
        if found != SELFTEST_EXPECTED:
            print("самопроверка: ожидались нарушения " + str(sorted(SELFTEST_EXPECTED)), file=sys.stderr)
            print("самопроверка: получены нарушения " + str(sorted(found)), file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они есть, "
          f"и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Проверка направления зависимостей между слоями.")
    parser.add_argument("paths", nargs="*", type=Path, help="что проверять; по умолчанию весь репозиторий")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    root = Path(__file__).resolve().parent.parent
    paths = [path.resolve() for path in arguments.paths] or [root]
    for path in paths:
        if not path.exists():
            print(f"нет такого пути: {path}", file=sys.stderr)
            return 2

    violations, checked = check(paths, root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Зависимости направлены только внутрь: "
              f"core ничего не знает, application знает core, infrastructure знает обоих.",
              file=sys.stderr)
        return 1

    print(f"Проверено файлов: {checked}. Нарушений нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))