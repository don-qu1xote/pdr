#!/usr/bin/env python3
"""Колонка uuid читается адаптером как текст, а не как что придётся.

Драйвер Postgres в userver не умеет отдавать `uuid` в `std::string` — он
отказывается, и отказывается В РАНТАЙМЕ:

    Query result set field `person_id` doesn't have a parser.
    Database type is 'uuid' (oid: 2950)

Это ровно та поломка, которую не видит ни компилятор, ни unit-прогон, ни ревью:
адаптер собран, тесты на фейках зелёные, а первая же настоящая выборка отвечает
пятисоткой. Найдена она была не рассуждением, а поднятым сервисом — то есть в
самый дорогой момент, какой был доступен.

Правило: `select` адаптера, забирающий колонку типа `uuid`, обязан привести её к
тексту — `column::text as column`. Идентификаторы уезжают в домен строкой
(`core::StrongId::Parse`), и приведение — единственное место, где это сказано
базе.

ЧТО РАЗБИРАЕТСЯ. Файлы запросов `db/sql/<контекст>/*.sql` — с PDR-DB-05 запросы
живут там, а не строками в .cpp. Из файла берётся `select <список> from
<таблица>`; состав колонок таблиц читается из миграций тем же разбором, что у
остальных проверок схемы (scripts/migration_model.py). Чего разбор не понял, он
пропускает молча: это линтер одного правила, а не второй разборщик SQL. Пропуск
здесь безопасен — непонятый запрос просто не проверяется, а не объявляется
правильным.

Запуск:
    python3 scripts/check_uuid_reads.py
    python3 scripts/check_uuid_reads.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402  (после правки sys.path)

MIGRATIONS = Path("db/migrations")
QUERIES = Path("db/sql")

UUID_TYPE = "uuid"

COMMENT = re.compile(r"--[^\n]*")
SELECT = re.compile(r"\bselect\s+(?P<columns>.+?)\s+from\s+(?P<table>[a-z_][a-z0-9_]*)", re.I | re.S)

PLAIN_COLUMN = re.compile(r"^(?:(?P<alias>[a-z_][a-z0-9_]*)\.)?(?P<name>[a-z_][a-z0-9_]*)$", re.I)
"""Столбец, взятый как есть: ни функции, ни приведения, ни звёздочки."""


def query_files(root: Path) -> Iterator[Path]:
    directory = root / QUERIES
    if not directory.is_dir():
        return
    for path in sorted(directory.rglob("*.sql")):
        if path.is_file():
            yield path


def statement(text: str) -> str:
    """Текст запроса без пояснений: комментарий — не часть выборки.

    В файле запроса они по-русски и по делу (docs/comments.md разрешает их в
    SQL), но `-- берём id из identity_person` разбору выборки только мешает.
    """
    return COMMENT.sub(" ", text)


def selected_columns(columns: str) -> Iterator[str]:
    """Имена колонок из списка выборки — только взятые как есть.

    Всё остальное — приведения, функции, звёздочка, подзапросы — пропускается:
    правило про голую колонку, а не про выражение.
    """
    depth = 0
    piece: list[str] = []
    for symbol in columns + ",":
        if symbol == "(":
            depth += 1
        elif symbol == ")":
            depth -= 1
        if symbol == "," and depth == 0:
            yield "".join(piece).strip()
            piece = []
            continue
        piece.append(symbol)


def uuid_columns(root: Path) -> dict[str, set[str]]:
    directory = root / MIGRATIONS
    if not directory.is_dir():
        return {}

    tables = model.merged_tables(model.load(directory))
    return {
        name: {column.name for column in table.columns if column.type.startswith(UUID_TYPE)}
        for name, table in tables.items()
    }


def check(root: Path) -> tuple[list[str], int]:
    known = uuid_columns(root)
    violations: list[str] = []
    checked = 0

    for path in query_files(root):
        display = path.relative_to(root)
        query = statement(path.read_text(encoding="utf-8", errors="replace"))

        for found in SELECT.finditer(query):
            checked += 1

            table = found.group("table").lower()
            if table not in known or not known[table]:
                continue

            number = query[: found.start()].count("\n") + 1
            for piece in selected_columns(found.group("columns")):
                shape = PLAIN_COLUMN.match(piece)
                if shape is None:
                    continue
                name = shape.group("name").lower()
                if name in known[table]:
                    violations.append(
                        f"{display}:{number}: колонка «{name}» таблицы {table} имеет тип uuid, "
                        f"а выбрана как есть. Драйвер откажется отдавать её строкой — и "
                        f"откажется в рантайме: «{name}::text as {name}»"
                    )

    return violations, checked


SELFTEST_MIGRATION = """
create table identity_person (
    tenant_id    uuid        not null,
    id           uuid        not null,
    display_name text        not null,
    born_on      date
);
"""

SELFTEST_FILES = {
    "db/sql/alpha/bad.sql": (
        "SELECT id, display_name\n"
        "  FROM identity_person\n"
        " WHERE tenant_id = $1::uuid\n"
    ),
    "db/sql/alpha/good.sql": (
        "SELECT id::text AS id, display_name, born_on\n"
        "  FROM identity_person\n"
        " WHERE tenant_id = $1::uuid\n"
    ),
    "db/sql/alpha/qualified.sql": "SELECT p.id, p.display_name FROM identity_person p\n",
    "db/sql/alpha/counting.sql": "SELECT count(*) FROM identity_person\n",
    "db/sql/alpha/elsewhere.sql": "SELECT id, name FROM unknown_table\n",
    "db/sql/alpha/commented.sql": (
        "-- SELECT id, display_name FROM identity_person\n"
        "SELECT display_name FROM identity_person\n"
    ),
}

SELFTEST_EXPECTED = (
    ("db/sql/alpha/bad.sql", "«id»"),
    ("db/sql/alpha/qualified.sql", "«id»"),
)

SELFTEST_CLEAN = (
    "db/sql/alpha/good.sql",
    "db/sql/alpha/counting.sql",
    "db/sql/alpha/elsewhere.sql",
    "db/sql/alpha/commented.sql",
)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить ровно то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        migration = root / MIGRATIONS / "V001__people.sql"
        migration.parent.mkdir(parents=True, exist_ok=True)
        migration.write_text(SELFTEST_MIGRATION, encoding="utf-8")

        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, _ = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано {fragment} в {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильный запрос объявлен нарушением: {name}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Колонка uuid читается адаптером как текст.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, checked = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Идентификатор уезжает в домен строкой, и "
              f"сказать это базе можно только приведением", file=sys.stderr)
        return 1

    print(f"Выборок разобрано: {checked} в {QUERIES}. Колонки uuid читаются текстом.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
