#!/usr/bin/env python3
"""Линтер миграций: правила колонок, которые нельзя оставлять на ревью.

Правила применяют в двадцати задачах после этой. Правило, которое проверяется
только глазами, через десять задач перестаёт соблюдаться — поэтому здесь:

* `tenant_id uuid not null` на КАЖДОЙ доменной таблице. Исключения — только
  метатаблицы механизма, и они перечислены поимённо ниже;
* время — `timestamptz`. `timestamp` без зоны не встречается нигде: это ровно
  та ошибка, из-за которой потом переписывают календари. Где важна зона, в
  которой человек задумывал событие, рядом стоит `tz text` с идентификатором
  IANA («Europe/Moscow»);
* деньги — `amount_minor bigint` плюс `currency char(3)`. Ни numeric, ни float,
  ни double рядом с суммой. Никогда.

Нарушение печатается как <файл>:<строка> и даёт код возврата 1.

Запуск:
    python3 scripts/check_migrations.py
    python3 scripts/check_migrations.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402  (после правки sys.path)

# Таблицы механизма перечислены в модели: тем же списком пользуется сверка
# владения таблицами, и разъехаться им негде.
META_TABLES = model.META_TABLES

# Типы времени без зоны. timestamptz и date разрешены, остальное — нет.
TIME_WITHOUT_ZONE = {
    "timestamp",
    "timestamp without time zone",
    "time",
    "time without time zone",
}

# Дробные типы: рядом с деньгами их не бывает.
FRACTIONAL = ("numeric", "decimal", "real", "double precision", "float", "money")

# Слова, после которых колонка — точно деньги.
STRICT_MONEY = ("amount", "price", "cost", "fee", "payout", "refund")
# Слова, после которых колонка МОЖЕТ быть деньгами: «12 из 20» тоже total.
SOFT_MONEY = STRICT_MONEY + ("sum", "total", "balance", "discount")


def _has_word(name: str, words: Sequence[str]) -> bool:
    parts = set(name.split("_"))
    return any(word in parts for word in words)


def _is_fractional(column_type: str) -> bool:
    return any(column_type.startswith(kind) for kind in FRACTIONAL)


def check_table(table: model.Table, source: str) -> list[str]:
    violations: list[str] = []
    place = f"{source}:{table.line}"
    names = {column.name for column in table.columns}

    if table.name not in META_TABLES:
        tenant = next((column for column in table.columns if column.name == "tenant_id"), None)
        if tenant is None:
            violations.append(
                f"{place}: таблица {table.name} без tenant_id. Арендатор есть на каждой "
                f"доменной таблице — изоляция структурная, а не по дисциплине"
            )
        else:
            if tenant.type != "uuid":
                violations.append(
                    f"{source}:{tenant.line}: tenant_id должен быть uuid, а не {tenant.type}"
                )
            if "not null" not in tenant.definition.lower():
                violations.append(
                    f"{source}:{tenant.line}: tenant_id обязан быть not null — "
                    f"строка без арендатора не принадлежит никому"
                )

    for column in table.columns:
        where = f"{source}:{column.line}"

        if column.type in TIME_WITHOUT_ZONE:
            violations.append(
                f"{where}: {table.name}.{column.name} — {column.type}. Время хранится "
                f"как timestamptz в UTC; зона задумывания события живёт отдельной "
                f"колонкой tz text"
            )

        if (column.name == "tz" or column.name.endswith("_tz")) and column.type != "text":
            violations.append(
                f"{where}: {table.name}.{column.name} — зона IANA, это text, а не {column.type}"
            )

        if column.name.endswith("_minor") and column.type != "bigint":
            violations.append(
                f"{where}: {table.name}.{column.name} — минорные единицы, это bigint, "
                f"а не {column.type}"
            )

        if _has_word(column.name, SOFT_MONEY) and _is_fractional(column.type):
            violations.append(
                f"{where}: {table.name}.{column.name} — {column.type} рядом с суммой. "
                f"Деньги: amount_minor bigint плюс currency char(3), ни одного дробного типа"
            )

        if _has_word(column.name, STRICT_MONEY) and not column.name.endswith("_minor"):
            violations.append(
                f"{where}: {table.name}.{column.name} — сумма должна называться "
                f"*_minor и храниться целыми минорными единицами"
            )

        if column.name == "currency" and column.type != "char(3)":
            violations.append(
                f"{where}: {table.name}.currency — код валюты, это char(3), а не {column.type}"
            )

    if any(column.name.endswith("_minor") for column in table.columns) and "currency" not in names:
        violations.append(
            f"{place}: у таблицы {table.name} есть сумма, но нет currency char(3). "
            f"Сумма без валюты — не сумма"
        )

    return violations


def check(directory: Path, root: Path) -> tuple[list[str], int]:
    try:
        migrations = model.load(directory)
    except model.MigrationError as error:
        return ([str(error)], 0)

    violations: list[str] = []
    tables = 0
    for migration in migrations:
        try:
            source = str(migration.path.relative_to(root))
        except ValueError:
            source = migration.path.name
        for table in migration.tables:
            tables += 1
            violations.extend(check_table(table, source))

    return violations, tables


SELFTEST_FILES = {
    # Чистый случай: так выглядит правильная доменная таблица.
    "V001__good.sql": """
create table billing_invoice (
    tenant_id    uuid        not null,
    id           uuid        primary key,
    issued_at    timestamptz not null,
    tz           text        not null,
    amount_minor bigint      not null,
    currency     char(3)     not null,
    constraint billing_invoice_amount_positive check (amount_minor > 0)
);
""",
    # Таблица без арендатора.
    "V002__no_tenant.sql": """
create table scheduling_slot (
    id        uuid        primary key,
    starts_at timestamptz not null
);
""",
    # Время без зоны и зона не тем типом.
    "V003__naive_time.sql": """
create table scheduling_lesson (
    tenant_id uuid      not null,
    id        uuid      primary key,
    starts_at timestamp not null,
    tz        timestamptz not null
);
""",
    # Дробные деньги и сумма, названная не по правилу.
    "V004__money.sql": """
create table billing_payment (
    tenant_id uuid          not null,
    id        uuid          primary key,
    amount    numeric(10,2) not null,
    total     double precision not null
);
""",
    # Сумма есть, валюты нет.
    "V005__no_currency.sql": """
create table billing_package (
    tenant_id   uuid   not null,
    id          uuid   primary key,
    price_minor bigint not null
);
""",
}

SELFTEST_EXPECTED = {
    ("V002__no_tenant.sql", "без tenant_id"),
    ("V003__naive_time.sql", "timestamp"),
    ("V003__naive_time.sql", "зона IANA"),
    ("V004__money.sql", "рядом с суммой"),
    ("V004__money.sql", "*_minor"),
    ("V005__no_currency.sql", "нет currency"),
}


def selftest() -> int:
    """Отрицательные случаи: линтер обязан ловить то, ради чего он написан."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        migrations = root / "db" / "migrations"
        migrations.mkdir(parents=True)
        for name, text in SELFTEST_FILES.items():
            (migrations / name).write_text(text, encoding="utf-8")

        violations, tables = check(migrations, root)
        if tables != len(SELFTEST_FILES):
            print(f"самопроверка: разобрано {tables} таблиц из {len(SELFTEST_FILES)}",
                  file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» в {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if any("V001__good.sql" in line for line in violations):
            print("самопроверка: правильная таблица объявлена нарушением", file=sys.stderr)
            for line in violations:
                print("    " + line, file=sys.stderr)
            return 1

        # Имя файла не по правилу — тоже отказ.
        (migrations / "V006-wrong-name.sql").write_text("select 1;\n", encoding="utf-8")
        broken, _ = check(migrations, root)
        if not any("не по правилу" in line for line in broken):
            print("самопроверка: имя файла не по правилу не поймано", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они есть, "
          f"и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Линтер миграций: правила колонок.")
    parser.add_argument("--dir", type=Path, default=root / "db/migrations")
    parser.add_argument("--selftest", action="store_true", help="проверить сам линтер и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, tables = check(arguments.dir, root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Правила колонок — "
              f"docs/architecture/migrations.md", file=sys.stderr)
        return 1

    print(f"Миграции проверены, таблиц: {tables}. Нарушений нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
