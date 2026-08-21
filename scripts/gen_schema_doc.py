#!/usr/bin/env python3
"""Документ схемы собирается из миграций, а не пишется руками.

Руками написанный документ схемы расходится с базой на второй неделе, и дальше
ему верят и ошибаются. Здесь он порождается из тех же файлов, которые применяет
scripts/migrate.py, — разойтись им негде.

    python3 scripts/gen_schema_doc.py           пересобрать документ
    python3 scripts/gen_schema_doc.py --check   не менять, а проверить свежесть
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402  (после правки sys.path)

HEADER = """# Схема базы

<!-- Файл собран scripts/gen_schema_doc.py из db/migrations. Руками не правится:
     правка переживёт ровно до следующей пересборки. Изменить схему — значит
     написать новую миграцию. -->

Собрано из миграций: {count}. Таблиц: {tables}.

Правила, которым подчиняется каждая колонка, — в
[migrations.md](migrations.md). Как устроена изоляция арендаторов и почему у
каждой доменной таблицы обязана быть политика — в [tenancy.md](tenancy.md);
отсутствие политики роняет сборку (`scripts/check_rls.py`).
"""


def _isolation(table: str, migrations: Sequence[model.Migration]) -> list[str]:
    """Строки про изоляцию: что включено и какие политики лежат на таблице."""
    if table in model.META_TABLES:
        return [f"\nНе доменная таблица: {model.META_TABLES[table]}. Арендатора и политики "
                f"у неё нет.\n"]

    actions = [
        change.action
        for migration in migrations
        for change in migration.row_security
        if change.table == table
    ]
    policies = [
        policy for migration in migrations for policy in migration.policies if policy.table == table
    ]

    state = "включена" if "enable" in actions else "НЕ ВКЛЮЧЕНА"
    if "force" in actions:
        state += " и форсирована"
    lines = [f"\nПострочная защита {state}.\n"]

    if policies:
        lines.append("\nПолитики:\n\n")
        for policy in policies:
            lines.append(f"* `{policy.name}` — `{policy.body}`\n")
    return lines


def render(migrations: Sequence[model.Migration]) -> str:
    tables = [(migration, table) for migration in migrations for table in migration.tables]

    lines = [HEADER.format(count=len(migrations), tables=len(tables))]

    if not tables:
        lines.append("\nТаблиц в миграциях пока нет.\n")
        return "".join(lines)

    lines.append("\n## Таблицы\n")
    for migration, table in sorted(tables, key=lambda pair: pair[1].name):
        lines.append(f"\n### {table.name}\n")
        if table.comment:
            lines.append(f"\n{table.comment}\n")
        lines.append(f"\nЗаведена миграцией `{migration.file_name}`.\n")
        lines.append("\n| Колонка | Тип | Определение |\n| --- | --- | --- |\n")
        for column in table.columns:
            definition = column.definition[len(column.name) :].strip()
            lines.append(f"| `{column.name}` | `{column.type}` | {definition} |\n")
        if table.constraints:
            lines.append("\nОграничения:\n\n")
            for constraint in table.constraints:
                lines.append(f"* `{constraint}`\n")

        indexes = [
            index for source in migrations for index in source.indexes if index.table == table.name
        ]
        if indexes:
            lines.append("\nИндексы:\n\n")
            for index in indexes:
                kind = "уникальный" if index.unique else "обычный"
                lines.append(f"* `{index.name}` — {kind}, `{index.body}`\n")

        lines.extend(_isolation(table.name, migrations))

    lines.append("\n## Порядок применения\n\n")
    for migration in migrations:
        names = ", ".join(table.name for table in migration.tables) or "без новых таблиц"
        lines.append(f"1. `{migration.file_name}` — {names}\n")

    return "".join(lines)


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Документ схемы из миграций.")
    parser.add_argument("--dir", type=Path, default=root / "db/migrations")
    parser.add_argument("--out", type=Path, default=root / "docs/architecture/schema.md")
    parser.add_argument("--check", action="store_true",
                        help="не переписывать, а проверить, что документ свежий")
    arguments = parser.parse_args(argv)

    try:
        migrations = model.load(arguments.dir)
    except model.MigrationError as error:
        print(str(error), file=sys.stderr)
        return 1

    for migration in migrations:
        problems = model.unsupported(migration.sql, migration.file_name)
        for line in problems:
            print(line, file=sys.stderr)
        if problems:
            return 1

    expected = render(migrations)

    if not arguments.check:
        arguments.out.parent.mkdir(parents=True, exist_ok=True)
        arguments.out.write_text(expected, encoding="utf-8")
        print(f"Собрано: {arguments.out.relative_to(root)} — таблиц "
              f"{sum(len(migration.tables) for migration in migrations)}.")
        return 0

    current = arguments.out.read_text(encoding="utf-8") if arguments.out.is_file() else ""
    if current == expected:
        print(f"{arguments.out.relative_to(root)} совпадает с миграциями.")
        return 0

    print(f"{arguments.out.relative_to(root)} разошёлся с миграциями. Пересоберите: "
          f"make schema-doc", file=sys.stderr)
    for line in list(difflib.unified_diff(
        current.splitlines(), expected.splitlines(),
        fromfile="на диске", tofile="из миграций", lineterm=""
    ))[:40]:
        print("    " + line, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
