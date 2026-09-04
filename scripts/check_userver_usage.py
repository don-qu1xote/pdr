#!/usr/bin/env python3
"""Штатное вместо самодельного — на стороне C++ (ADR-0013, PDR-INF-04).

Таблица ADR-0013 — четырнадцать строк, и до этой проверки на C++-часть их не
смотрел никто, кроме человека. Правило, которое соблюдается вниманием,
соблюдается ровно до третьего подрядчика в свежей сессии.

ЧТО ЛОВИТСЯ ЗДЕСЬ

* БЛОКИРУЮЩАЯ СИНХРОНИЗАЦИЯ. `std::mutex`, `std::condition_variable`,
  `std::lock_guard`, `std::future` и прочее из `<mutex>`/`<future>` в корутинном
  рантайме останавливают ПОТОК, а не сопрограмму: сопрограммы, делящие поток,
  встают вместе с ним, и метрикам это не видно вовсе — задача не «медленная», её
  просто нет. Штатное: `engine::Mutex`, `engine::SharedMutex`,
  `engine::ConditionVariable`, `engine::Semaphore`, `engine::SingleUseEvent`,
  `utils::Async` с `engine::Future`. `std::atomic` при этом РАЗРЕШЁН: он не
  паркует поток;
* БЛОКИРУЮЩИЙ СОН. `sleep_until`, `usleep`, `nanosleep`, `sleep` — то же самое и
  по той же причине. Штатное: `engine::SleepFor`, `engine::SleepUntil`,
  `engine::InterruptibleSleepFor`;
* SQL-СТРОКА В .cpp. С PDR-DB-05 текст запроса живёт в `db/sql/<контекст>/*.sql`,
  а объявление порождает `userver_add_sql_library`. Строка в коде — это запрос
  БЕЗ ИМЕНИ, а по имени назначаются и срок (`POSTGRES_QUERIES_COMMAND_CONTROL`),
  и строка метрик: безымянный молча уезжает на общий пол;
* JSON В СИГНАТУРЕ application. После PDR-API-04 в сценарий приходит структура,
  порождённая из схемы, а не `formats::json::Value`. Сценарий, принимающий
  JSON, — это разбор тела, переехавший на слой, которому про транспорт знать
  нечего;
* СВОЙ ВЕБСОКЕТ. Штатное: `server::websocket`;
* СВОЙ РЕЕСТР МЕТРИК И СВОЯ ГИСТОГРАММА. Штатное: `utils::statistics::Histogram`
  (границы корзин объявляются, всё остальное делает userver),
  `utils::statistics::Rate`, `MetricTag`, `RegisterWriter`.

ЧЕГО ЗДЕСЬ НЕТ И У КОГО ЭТО ЛЕЖИТ. Проверка не повторяет чужие правила — два
сообщения об одном нарушении хуже одного, и чинят их в двух местах:

* `std::thread`, `.detach()`, `std::this_thread::sleep_for`, `std::async`, вторая
  JSON-библиотека, свой LRU, свой ограничитель скорости, свой размыкатель цепи,
  свой откат, сторонний HTTP-клиент — `scripts/check_handmade.py`;
* объявление `postgres::Query` руками и сроки запросов по именам —
  `scripts/check_query_timeouts.py`;
* второй наследник `HttpHandlerBase`, зовущий форму, и разбор тела руками в
  ручке — `scripts/check_http_form.py`;
* границы слоёв и контекстов по `#include` — `scripts/check_layers.py`. JSON в
  application ловится ЗДЕСЬ по имени типа: `check_layers.py` разбирает директивы
  препроцессора и типа, пришедшего транзитивно, не увидит;
* обращение наружу вне адаптера интеграции — `scripts/check_sovereignty.py`.

ЧЕГО ПРОВЕРКА НЕ ТРЕБУЕТ И ПОЧЕМУ. `CommandControl` у каждого `Execute` и
`Begin` — НЕ требуется, и это решение, а не пропуск. В этом дереве срок запроса
живёт в динамическом конфиге и назначается ПО ИМЕНИ запроса (PDR-API-05,
`POSTGRES_DEFAULT_COMMAND_CONTROL` как пол и `POSTGRES_QUERIES_COMMAND_CONTROL`
поимённо); `CommandControl` в коде — это тот же срок, но константой, то есть
ровно то, что запрещено PDR-CFG-01. Свойство «у запроса есть свой срок»
держится другим концом: безымянных запросов не бывает, и это стережёт запрет
SQL-строки выше вместе с `check_query_timeouts.py`.

ТЕСТЫ НЕ ПРОВЕРЯЮТСЯ ВОВСЕ. Двойник таблицы держит строки в `std::map` под
`std::mutex` и делает это правильно: он не в рантайме сервиса. Живой набор
выполняет SQL строкой, потому что готовит СВОЮ таблицу, а не доменную.

ОТСТУПЛЕНИЕ пишется в той же строке с причиной:

    std::mutex lock_;  // userver-ok: разделяемое состояние живёт вне сопрограмм

Ключ греп-совместим: `grep -rn "userver-ok:"` показывает все отступления разом.

Запуск:
    python3 scripts/check_userver_usage.py
    python3 scripts/check_userver_usage.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SEARCHED_ROOTS = ("libs", "services")
SOURCE_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".ipp", ".c", ".cc", ".cpp", ".cxx"})
SKIPPED_DIRS = frozenset({".git", "build", "out", "node_modules", "_deps", "third_party",
                          "compile_fail", "__pycache__", "tests"})
SKIPPED_PREFIXES = ("build", "venv", ".venv")

APPLICATION = "application"

WAIVER = re.compile(r"//\s*userver-ok:\s*(\S.*)$")

Rule = tuple[str, re.Pattern[str], str]

RULES: tuple[Rule, ...] = (
    (
        "блокирующая синхронизация",
        re.compile(
            r"\bstd::(?:mutex|recursive_mutex|timed_mutex|recursive_timed_mutex|"
            r"shared_mutex|shared_timed_mutex|lock_guard|unique_lock|scoped_lock|"
            r"shared_lock|condition_variable|condition_variable_any|counting_semaphore|"
            r"binary_semaphore|latch|barrier|future|shared_future|promise|packaged_task|"
            r"call_once|once_flag)\b"
        ),
        "в корутинном рантайме это остановленный ПОТОК, а не сопрограмма: вставшая "
        "сопрограмма уносит с собой всех, кто делит с ней поток, и метрикам этого не "
        "видно. Штатное: engine::Mutex, engine::SharedMutex, engine::ConditionVariable, "
        "engine::Semaphore, engine::SingleUseEvent, utils::Async. std::atomic разрешён",
    ),
    (
        "блокирующий сон",
        re.compile(r"\bstd::this_thread::sleep_until\b|\b(?:usleep|nanosleep)\s*\(|"
                   r"(?<![\w:])sleep\s*\("),
        "спящий поток не отдаёт сопрограмму планировщику и не отменяется по дедлайну. "
        "Штатное: engine::SleepFor, engine::SleepUntil, engine::InterruptibleSleepFor",
    ),
    (
        "свой вебсокет",
        re.compile(r"\b(?:class|struct)\s+\w*(?:WebSocket|Websocket)\w*\b|"
                   r"\"Sec-WebSocket-\w+\""),
        "рукопожатие, кадрирование и пинги — это протокол, а не двадцать строк. "
        "Штатное: server::websocket",
    ),
    (
        "свой реестр метрик",
        re.compile(r"\b(?:class|struct)\s+\w*(?:Histogram|Buckets|MetricRegistry|"
                   r"MetricsRegistry|CounterRegistry|StatsRegistry|StatsCollector|"
                   r"MetricsCollector)\b"),
        "у метрик уже есть хранилище, метки, гистограмма с корзинами и отдача на "
        "служебном порту. Штатное: utils::statistics::Histogram, utils::statistics::Rate, "
        "MetricTag, RegisterWriter",
    ),
)

SQL_VERB = re.compile(r"(?i)(?<![\w.])(?:select|insert|update|delete|create|alter|drop|truncate)"
                      r"(?![\w.])")
SQL_SHAPE = re.compile(r"(?i)(?<![\w.])(?:from|into|values|set|where|table|join|returning)"
                       r"(?![\w.])")
LITERAL = re.compile(r'"(?:\\.|[^"\\])*"|R"([^(]*)\((?:.|\n)*?\)\1"')

JSON_TYPE = re.compile(r"\bformats::json::\w+")


def sources(root: Path) -> Iterator[Path]:
    for name in SEARCHED_ROOTS:
        base = root / name
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            relative = path.relative_to(root)
            if any(part in SKIPPED_DIRS or part.startswith(SKIPPED_PREFIXES)
                   for part in relative.parts):
                continue
            yield relative


def code_of(text: str) -> str:
    """Текст без комментариев, с сохранением нумерации строк.

    Строковые литералы ОСТАЮТСЯ: SQL живёт именно в них, и вырезать их значило
    бы отменить одну из проверок. Комментарии убираются, чтобы doc-комментарий
    «своего вебсокета не пишем» не считался своим вебсокетом.
    """
    out: list[str] = []
    index = 0
    size = len(text)
    while index < size:
        if text[index] == '"':
            found = LITERAL.match(text, index)
            if found is not None:
                out.append(found.group(0))
                index = found.end()
                continue
        if text.startswith("//", index):
            while index < size and text[index] != "\n":
                out.append(" ")
                index += 1
            continue
        if text.startswith("/*", index):
            while index < size and not text.startswith("*/", index):
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
            out.append("  ")
            index += 2
            continue
        out.append(text[index])
        index += 1
    return "".join(out)


def check_rules(root: Path, path: Path, code: str, lines: Sequence[str]) -> list[str]:
    violations: list[str] = []
    for number, line in enumerate(code.splitlines(), start=1):
        original = lines[number - 1] if number <= len(lines) else ""
        if WAIVER.search(original):
            continue
        for name, pattern, instead in RULES:
            found = pattern.search(line)
            if found is None:
                continue
            violations.append(
                f"{path}:{number}: {name} — «{found.group(0).strip()}». {instead}. "
                f"Если отступить всё-таки нужно, напишите в этой строке "
                f"«// userver-ok: причина»"
            )
    return violations


def check_sql(path: Path, code: str, lines: Sequence[str]) -> list[str]:
    """Текст запроса в литерале.

    Смотрится не одно слово, а ДВА: `select` рядом с `from`, `insert` рядом с
    `into`. Одного слова мало — «update» встречается в человеческом тексте, и
    проверка, краснеющая на слове, отключается первой.
    """
    violations: list[str] = []
    for found in LITERAL.finditer(code):
        body = found.group(0)
        if not (SQL_VERB.search(body) and SQL_SHAPE.search(body)):
            continue
        number = code.count("\n", 0, found.start()) + 1
        span = range(number - 1, min(number + body.count("\n"), len(lines)))
        if any(WAIVER.search(lines[line]) for line in span):
            continue
        violations.append(
            f"{path}:{number}: текст запроса строкой в коде. Такой запрос БЕЗ ИМЕНИ, а по "
            f"имени назначаются и срок (POSTGRES_QUERIES_COMMAND_CONTROL), и строка метрик: "
            f"безымянный молча уезжает на общий пол. Место запроса — db/sql/<контекст>/*.sql, "
            f"объявление порождает userver_add_sql_library (PDR-DB-05). Если отступить "
            f"всё-таки нужно, напишите в этой строке «// userver-ok: причина»"
        )
    return violations


def check_application_json(path: Path, code: str, lines: Sequence[str]) -> list[str]:
    """JSON на слое сценариев.

    Ловится ИМЯ ТИПА, а не директива `#include`: её разбирает check_layers.py, а
    тип, пришедший транзитивно через чужой заголовок, ему не виден.
    """
    if APPLICATION not in path.parts:
        return []

    violations: list[str] = []
    for number, line in enumerate(code.splitlines(), start=1):
        original = lines[number - 1] if number <= len(lines) else ""
        if WAIVER.search(original):
            continue
        found = JSON_TYPE.search(line)
        if found is None:
            continue
        violations.append(
            f"{path}:{number}: «{found.group(0)}» на слое сценариев. После PDR-API-04 в "
            f"сценарий приходит структура, порождённая из схемы, а не JSON: сценарий, "
            f"принимающий JSON, — это разбор тела, переехавший туда, где про транспорт "
            f"знать нечего. Если отступить всё-таки нужно, напишите в этой строке "
            f"«// userver-ok: причина»"
        )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    checked = 0

    for path in sources(root):
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        code = code_of(text)
        lines = text.splitlines()
        checked += 1

        violations.extend(check_rules(root, path, code, lines))
        violations.extend(check_sql(path, code, lines))
        violations.extend(check_application_json(path, code, lines))

    return violations, checked


SELFTEST_FILES = {
    "libs/pdr-x/src/x/infrastructure/blocking.cpp": (
        "#include <mutex>\n"
        "std::mutex lock_;\n"
    ),
    "libs/pdr-x/src/x/infrastructure/napping.cpp": (
        "void Wait() {\n"
        "    usleep(1000);\n"
        "}\n"
    ),
    "libs/pdr-x/src/x/infrastructure/socket_room.cpp": (
        "class RoomWebSocket final {\n"
        "};\n"
    ),
    "libs/pdr-x/src/x/infrastructure/counting.cpp": (
        "class LatencyHistogram final {\n"
        "};\n"
    ),
    "libs/pdr-x/src/x/infrastructure/postgres_rooms.cpp": (
        "void Load(Session& session) {\n"
        '    session.Execute("SELECT id, name FROM rooms WHERE tenant_id = $1");\n'
        "}\n"
    ),
    "libs/pdr-x/src/x/application/book_lesson.hpp": (
        "class BookLesson final {\n"
        "public:\n"
        "    void Run(const userver::formats::json::Value& body);\n"
        "};\n"
    ),
    "libs/pdr-x/src/x/infrastructure/waived.cpp": (
        "std::mutex shared_;  // userver-ok: состояние живёт вне сопрограмм, "
        "разделяют его потоки логгера\n"
        "void Wait() {\n"
        "    usleep(1000);  // userver-ok: тот же случай\n"
        "}\n"
        "class OldWebSocket final {  // userver-ok: чужой протокол, штатного нет\n"
        "};\n"
        "class OwnHistogram final {  // userver-ok: то же\n"
        "};\n"
        'const auto sql = "SELECT 1 FROM heartbeat";  // userver-ok: проба живости, '
        "имени ей не нужно\n"
    ),
    "libs/pdr-x/src/x/application/waived.hpp": (
        "void Run(const userver::formats::json::Value& body);  "
        "// userver-ok: чужой формат без схемы\n"
    ),
    "libs/pdr-x/src/x/infrastructure/clean.cpp": (
        "#include <userver/engine/mutex.hpp>\n"
        "#include <userver/utils/statistics/histogram.hpp>\n"
        "#include <pdr/sql_queries.hpp>\n"
        "\n"
        "userver::engine::Mutex lock_;\n"
        "std::atomic<int> counted_{0};\n"
        "constexpr std::array<double, 3> kDurationBoundsMs{5.0, 50.0, 500.0};\n"
        "void Load(Session& session) {\n"
        "    session.Execute(sql::kRoomsList);\n"
        "}\n"
        '/// Своего вебсокета и своего std::mutex здесь нет: комментарий не код.\n'
    ),
    "libs/pdr-x/src/x/infrastructure/tests/fake_rooms.hpp": (
        "#include <mutex>\n"
        "std::mutex lock_;\n"
        'const auto sql = "SELECT id FROM rooms";\n'
    ),
}

SELFTEST_EXPECTED = (
    ("blocking.cpp", "блокирующая синхронизация"),
    ("napping.cpp", "блокирующий сон"),
    ("socket_room.cpp", "свой вебсокет"),
    ("counting.cpp", "свой реестр метрик"),
    ("postgres_rooms.cpp", "текст запроса строкой в коде"),
    ("book_lesson.hpp", "на слое сценариев"),
)

SELFTEST_CLEAN = ("waived.cpp", "waived.hpp", "clean.cpp", "fake_rooms.hpp")


def selftest() -> int:
    """Отрицательные случаи: по одному на каждый запрет и по одному на исключение."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, checked = check(root)

        if checked != 9:
            print(f"самопроверка: просмотрено {checked} файлов вместо девяти "
                  f"(набор в tests/ не считается)", file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for clean in SELFTEST_CLEAN:
            if any(clean in line for line in violations):
                print(f"самопроверка: правильный случай объявлен нарушением: {clean}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        empty, nothing = check(Path(directory) / "нет-такого")
        if empty or nothing:
            print("самопроверка: на пустом дереве проверка нашла нарушения", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Штатное вместо самодельного на стороне C++: ADR-0013.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, checked = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Список штатного — "
              f"docs/adr/0013-standard-over-handmade.md", file=sys.stderr)
        return 1

    if not checked:
        print("Проверять нечего: исходников C++ в дереве ещё нет.")
        return 0

    print(f"Проверено файлов: {checked}. Самоделок вместо штатного в рантайме нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
