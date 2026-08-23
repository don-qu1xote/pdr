#!/usr/bin/env python3
"""Сверка миграций с картой контекстов: у каждой таблицы ровно один владелец.

Карта (docs/architecture/context-map.md) говорит, какой контекст какой таблицей
владеет. Этот скрипт проверяет, что карта и миграции не разошлись:

* таблица не может быть названа в разделе «Владеет» у двух контекстов;
* имя таблицы начинается с имени владельца — `billing_invoice`, а не `invoice`:
  чужое обращение тогда видно в самом запросе;
* каждая созданная миграцией таблица названа на карте.

Обратное неверно: таблица на карте без миграции — это нормально, карта идёт
впереди кода. А вот миграция впереди карты — нет: так карта и расходится с
кодом, а расходящейся карте верят и ошибаются.

Нарушение печатается как <файл>[:<строка>]: <причина> и даёт код возврата 1.

Запуск:
    python3 scripts/check_table_owners.py
    python3 scripts/check_table_owners.py --selftest
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

CONTEXT_HEADING = re.compile(r"^### ([a-z][a-z0-9_]*) — ", re.M)
OWNS_HEADING = "**Владеет:**"
OWNED_TABLE = re.compile(r"^\* `([a-z][a-z0-9_]*)`")
CREATE_TABLE = re.compile(
    r"\bcreate\s+table\s+(?:if\s+not\s+exists\s+)?[\"']?(?:public\.)?([a-z][a-z0-9_]*)",
    re.I,
)


def owners_from_map(text: str) -> tuple[dict[str, str], list[tuple[str, str]]]:
    """{таблица: контекст-владелец} и список пар (таблица, второй владелец)."""
    owners: dict[str, str] = {}
    duplicates: list[tuple[str, str]] = []

    sections = list(CONTEXT_HEADING.finditer(text))
    for index, heading in enumerate(sections):
        context = heading.group(1)
        end = sections[index + 1].start() if index + 1 < len(sections) else len(text)
        body = text[heading.end() : end]

        if OWNS_HEADING not in body:
            continue
        owns = body.split(OWNS_HEADING, 1)[1]
        owns = owns.split("\n\n**", 1)[0]

        for line in owns.splitlines():
            found = OWNED_TABLE.match(line.strip())
            if not found:
                continue
            table = found.group(1)
            if table in owners and owners[table] != context:
                duplicates.append((table, context))
                continue
            owners[table] = context

    return owners, duplicates


def created_tables(directory: Path) -> Iterator[tuple[Path, int, str]]:
    """(файл, строка, таблица) для каждой CREATE TABLE в миграциях."""
    if not directory.is_dir():
        return
    for path in sorted(directory.rglob("*.sql")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            for found in CREATE_TABLE.finditer(line):
                yield path, number, found.group(1)


def check(map_path: Path, migrations: Path, root: Path) -> tuple[list[str], int, int]:
    if not map_path.is_file():
        return ([f"{map_path}: карты контекстов нет, сверять миграции не с чем"], 0, 0)

    violations: list[str] = []
    owners, duplicates = owners_from_map(map_path.read_text(encoding="utf-8"))

    try:
        map_display = map_path.relative_to(root)
    except ValueError:
        map_display = map_path

    for table, second in duplicates:
        violations.append(
            f"{map_display}: таблица {table} названа владением сразу двух контекстов — "
            f"{owners[table]} и {second}; владелец бывает ровно один"
        )

    for table, context in sorted(owners.items()):
        if not table.startswith(f"{context}_"):
            violations.append(
                f"{map_display}: таблица {table} у контекста {context} названа не по владельцу; "
                f"ожидается имя, начинающееся с «{context}_»"
            )

    tables = 0
    for path, number, table in created_tables(migrations):
        tables += 1
        if table in owners:
            continue
        if table in model.META_TABLES:
            continue
        try:
            display = path.relative_to(root)
        except ValueError:
            display = path
        violations.append(
            f"{display}:{number}: таблица {table} не названа ни одним контекстом на карте; "
            f"задача, заводящая сущность, обязана назвать её контекст-владельца"
        )

    return violations, len(owners), tables


SELFTEST_MAP = """# Карта

## Контексты

### identity — люди

**Владеет:**

* `identity_person` — человек;
* `identity_consent` — согласия.

**Читает:** ничего.

### billing — деньги

**Владеет:**

* `billing_invoice` — счёт.

**Читает:** `identity` — кто платит (контракт).
"""

SELFTEST_BROKEN_MAP = SELFTEST_MAP.replace(
    "* `billing_invoice` — счёт.",
    "* `billing_invoice` — счёт;\n* `identity_person` — и человек заодно;\n* `invoice_line` — строка счёта.",
)

SELFTEST_GOOD_MIGRATION = """create table identity_person (
    id uuid primary key,
    tenant_id uuid not null
);
"""

SELFTEST_BAD_MIGRATION = """CREATE TABLE IF NOT EXISTS billing_invoice (id uuid primary key);
CREATE TABLE schema_version (version integer primary key);
CREATE TABLE lesson_notes (id uuid primary key);
"""


def selftest() -> int:
    """Отрицательные случаи: проверка обязана падать и называть место."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        docs = root / "docs" / "architecture"
        migrations = root / "db" / "migrations"
        docs.mkdir(parents=True)
        migrations.mkdir(parents=True)

        map_path = docs / "context-map.md"
        map_path.write_text(SELFTEST_MAP, encoding="utf-8")
        (migrations / "0001_identity.sql").write_text(SELFTEST_GOOD_MIGRATION, encoding="utf-8")

        violations, owned, tables = check(map_path, migrations, root)
        if violations or owned != 3 or tables != 1:
            print(f"самопроверка: чистый случай не прошёл: {violations}", file=sys.stderr)
            return 1

        map_path.write_text(SELFTEST_BROKEN_MAP, encoding="utf-8")
        violations, _, _ = check(map_path, migrations, root)
        if len(violations) != 2:
            print(f"самопроверка: ожидались два нарушения карты, получено {violations}",
                  file=sys.stderr)
            return 1
        if not any("двух контекстов" in line for line in violations):
            print("самопроверка: не поймана таблица с двумя владельцами", file=sys.stderr)
            return 1
        if not any("invoice_line" in line for line in violations):
            print("самопроверка: не поймано имя не по владельцу", file=sys.stderr)
            return 1

        map_path.write_text(SELFTEST_MAP, encoding="utf-8")
        (migrations / "0002_notes.sql").write_text(SELFTEST_BAD_MIGRATION, encoding="utf-8")
        violations, _, _ = check(map_path, migrations, root)
        if len(violations) != 1 or "lesson_notes" not in violations[0]:
            print(f"самопроверка: не поймана таблица мимо карты: {violations}", file=sys.stderr)
            return 1
        if ":3:" not in violations[0]:
            print(f"самопроверка: в сообщении нет строки миграции: {violations[0]}",
                  file=sys.stderr)
            return 1

    print("Самопроверка пройдена: 4 нарушения найдены там, где они есть, "
          "и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Сверка миграций с картой контекстов.")
    parser.add_argument("--map", type=Path, default=root / "docs/architecture/context-map.md")
    parser.add_argument("--migrations", type=Path, default=root / "db/migrations")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, owned, tables = check(arguments.map, arguments.migrations, root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. У каждой таблицы ровно один контекст-владелец, "
              f"и он назван на карте.", file=sys.stderr)
        return 1

    print(f"Таблиц на карте: {owned}, создаётся миграциями: {tables}. Расхождений нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
