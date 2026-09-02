#!/usr/bin/env python3
"""У запроса к базе есть срок, и срок этот назначен по имени (PDR-API-05).

Зависший запрос при нашей схеме «одна транзакция на весь меняющий запрос» уносит
с собой транзакцию арендатора, занятый ключ повтора и соединение из пула. Пул
кончится раньше, чем кто-то заметит, и снаружи это будет «всё висит», а не
ошибка. Поэтому срок есть у каждого запроса: общий пол —
`POSTGRES_DEFAULT_COMMAND_CONTROL`, а тем, кому пол не годится, срок назначен
поимённо в `POSTGRES_QUERIES_COMMAND_CONTROL`.

Проверяется:

* У КАЖДОГО ЗАПРОСА ЕСТЬ ИМЯ. `postgres::Query` без `Query::Name` не получит
  своего срока никогда: назначать его не по чему. Такой запрос роняет сборку
  здесь же, а не обнаруживается через полгода зависшим;
* имя в реестре сроков СУЩЕСТВУЕТ в дереве. Строка про запрос, которого нет, —
  это срок, не доставшийся никому: запрос переименовали, а строку забыли, и он
  молча уехал на общий пол;
* имена запросов НЕ ПОВТОРЯЮТСЯ. Два разных запроса под одним именем делят один
  срок и одну строку метрик, и разобрать, который из них завис, нечем;
* сроки не переставлены местами: `statement_timeout_ms` меньше
  `network_timeout_ms`. Иначе база считает дольше, чем драйвер готов ждать, —
  запрос обрывается по сети, а в базе продолжает считаться.

ЧЕГО ПРОВЕРКА НЕ ТРЕБУЕТ. Имени в реестре сроков — не для каждого запроса:
большинству общий пол и годится, а список из сорока четырёх строк, где сорок
повторяют одно значение, никто не перечитывает. Сколько запросов живёт на полу,
проверка говорит вслух — чтобы это было решением, а не умолчанием.

Запуск:
    python3 scripts/check_query_timeouts.py
    python3 scripts/check_query_timeouts.py --selftest
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

REGISTRY = Path("configs/dynamic/registry.yaml")
VARIABLE = "POSTGRES_QUERIES_COMMAND_CONTROL"

SOURCE_SUFFIXES = frozenset({".hpp", ".cpp", ".hxx", ".cc"})
SKIPPED_DIRS = frozenset({".git", "out", "_deps", "__pycache__", "compile_fail"})
SKIPPED_PREFIXES = ("build", "venv", ".venv")

QUERY = re.compile(r"\bpostgres::Query\s+\w+\s*\{(?P<body>[^;]*)\}\s*;", re.S)
NAMED = re.compile(r"Query::Name\{\s*\"(?P<name>[^\"]+)\"")

ENTRY = re.compile(
    r"^\s{4}(?P<name>[a-z][a-z0-9_]*):\s*\{(?P<body>[^}]*)\}\s*$", re.M
)
MILLISECONDS = re.compile(r"\"(?P<field>network_timeout_ms|statement_timeout_ms)\":\s*(?P<value>\d+)")


def source_files(root: Path) -> Iterator[Path]:
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        parts = path.relative_to(root).parts
        if any(part in SKIPPED_DIRS or part.startswith(".") for part in parts):
            continue
        if any(part.startswith(SKIPPED_PREFIXES) for part in parts):
            continue
        yield path


def queries_in_code(root: Path) -> tuple[dict[str, list[Path]], list[str]]:
    """{имя запроса: где объявлен} и жалобы на запросы без имени."""
    found: dict[str, list[Path]] = {}
    violations: list[str] = []

    for path in source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "postgres::Query" not in text:
            continue
        display = path.relative_to(root)

        for query in QUERY.finditer(text):
            named = NAMED.search(query.group("body"))
            if not named:
                violations.append(
                    f"{display}: запрос объявлен без Query::Name. Срок назначается ПО ИМЕНИ, "
                    f"и безымянному его не назначить: он останется на общем поле навсегда"
                )
                continue
            found.setdefault(named.group("name"), []).append(display)

    for name, places in sorted(found.items()):
        if len(places) > 1:
            where = ", ".join(str(path) for path in places)
            violations.append(
                f"имя запроса «{name}» занято дважды: {where}. Два запроса под одним именем "
                f"делят срок и строку метрик, и который из них завис — не разобрать"
            )

    return found, violations


def timeouts_in_registry(text: str) -> tuple[dict[str, dict[str, int]], list[str]]:
    """{имя запроса: сроки} из блока значения в реестре."""
    start = text.find(f"\n{VARIABLE}:")
    if start < 0:
        return {}, []

    end = text.find("\n\n", start + 1)
    block = text[start : end if end > 0 else len(text)]

    found: dict[str, dict[str, int]] = {}
    for entry in ENTRY.finditer(block):
        milliseconds = {
            found_ms.group("field"): int(found_ms.group("value"))
            for found_ms in MILLISECONDS.finditer(entry.group("body"))
        }
        found[entry.group("name")] = milliseconds
    return found, []


def check(root: Path) -> tuple[list[str], int, int]:
    named, violations = queries_in_code(root)

    registry = root / REGISTRY
    if not registry.is_file():
        if named:
            violations.append(
                f"{REGISTRY}: реестра нет, а запросов с именами {len(named)}. Срок запроса "
                f"живёт в реестре: его правят без передеплоя"
            )
        return violations, len(named), 0

    timeouts, complaints = timeouts_in_registry(registry.read_text(encoding="utf-8"))
    violations.extend(complaints)

    for name in sorted(timeouts):
        if name not in named:
            violations.append(
                f"{REGISTRY}: сроку назначено имя «{name}», а такого запроса в дереве нет. "
                f"Запрос переименовали, а строку забыли — и он молча уехал на общий пол"
            )

    for name, milliseconds in sorted(timeouts.items()):
        network = milliseconds.get("network_timeout_ms")
        statement = milliseconds.get("statement_timeout_ms")
        if network is None or statement is None:
            violations.append(
                f"{REGISTRY}: у срока «{name}» назван не весь: нужны и network_timeout_ms, "
                f"и statement_timeout_ms"
            )
            continue
        if statement >= network:
            violations.append(
                f"{REGISTRY}: у срока «{name}» база считает дольше ({statement}), чем ждёт "
                f"драйвер ({network}). Запрос оборвётся по сети, а в базе продолжит считаться"
            )

    return violations, len(named), len(timeouts)


SELFTEST_REGISTRY = """PDR_SOMETHING:
  default: 1

POSTGRES_QUERIES_COMMAND_CONTROL:
  description: сроки по именам
  штатная: да
  default:
    identity_session_find: {"network_timeout_ms": 500, "statement_timeout_ms": 400}
    long_gone: {"network_timeout_ms": 500, "statement_timeout_ms": 400}
    upside_down: {"network_timeout_ms": 500, "statement_timeout_ms": 900}
    half_named: {"network_timeout_ms": 500}
  schema:
    type: object

USERVER_SOMETHING:
  default: true
"""

SELFTEST_FILES = {
    "libs/pdr-identity/src/identity/infrastructure/auth/sessions.cpp": (
        'const userver::storages::postgres::Query kFind{\n'
        '    "SELECT 1",\n'
        '    userver::storages::postgres::Query::Name{"identity_session_find"},\n'
        '};\n'
    ),
    "libs/pdr-identity/src/identity/infrastructure/auth/nameless.cpp": (
        'const userver::storages::postgres::Query kNameless{"SELECT 2"};\n'
    ),
    "libs/pdr-jobs/src/jobs/infrastructure/twin.cpp": (
        'const userver::storages::postgres::Query kTwin{\n'
        '    "SELECT 3",\n'
        '    userver::storages::postgres::Query::Name{"identity_session_find"},\n'
        '};\n'
    ),
}

SELFTEST_EXPECTED = (
    "объявлен без Query::Name",
    "занято дважды",
    "«long_gone», а такого запроса в дереве нет",
    "база считает дольше",
    "назван не весь",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ остаться без срока обязан ловиться."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        empty, named, timeouts = check(root)
        if empty or named or timeouts:
            print("самопроверка: пустое дерево объявлено нарушением", file=sys.stderr)
            return 1

        (root / REGISTRY).parent.mkdir(parents=True, exist_ok=True)
        (root / REGISTRY).write_text(SELFTEST_REGISTRY, encoding="utf-8")
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, _named, _timeouts = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if any("identity_session_find», а такого" in line for line in violations):
            print("самопроверка: существующий запрос объявлен несуществующим", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они есть, "
          f"и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="У запроса к базе есть срок, и он по имени.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, named, timeouts = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Сроки — {REGISTRY}", file=sys.stderr)
        return 1

    if not named:
        print("Запросов к базе нет — сроки назначать нечему.")
        return 0

    print(f"Запросов с именем: {named}. Свой срок назван у {timeouts}, "
          f"остальные {named - timeouts} живут на общем поле.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
