#!/usr/bin/env python3
"""Применение миграций и сверка контрольных сумм.

Механизм намеренно простой: файлы V001__имя.sql применяются по возрастанию
версии, каждая — в своей транзакции вместе с записью в реестр schema_version.
SQL исполняет psql; своего драйвера базы у проекта нет и не нужно.

**Применённая миграция никогда не редактируется.** Изменение — всегда новый
файл. Сверка сумм это не советует, а обеспечивает: если содержимое применённого
файла изменилось, `apply` и `verify` падают, а вместе с ними падает старт
установки. Решение записано в
docs/adr/0010-applied-migrations-are-never-edited.md.

Подключение — обычными переменными PG*: PGHOST, PGPORT, PGUSER, PGPASSWORD,
PGDATABASE. Их выставляет Makefile из профиля окружения.

Запуск:
    python3 scripts/migrate.py apply     применить всё, чего нет в реестре
    python3 scripts/migrate.py verify    ничего не менять, только сверить
    python3 scripts/migrate.py status    что применено, что ждёт
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402  (после правки sys.path)

REGISTRY = "schema_version"


class DatabaseError(Exception):
    """База недоступна или ответила ошибкой."""


def run_sql(sql: str, psql: str, single_transaction: bool = False) -> str:
    """Выполнить SQL через psql и вернуть его вывод."""
    command = [psql, "--no-psqlrc", "-v", "ON_ERROR_STOP=1", "-qtA", "-F", "|"]
    if single_transaction:
        command.append("--single-transaction")
    command += ["-f", "-"]

    try:
        result = subprocess.run(command, input=sql, capture_output=True, text=True, check=False)
    except FileNotFoundError as error:
        raise DatabaseError(f"нет {psql}: применять миграции нечем") from error

    if result.returncode != 0:
        raise DatabaseError(result.stderr.strip() or "psql завершился с ошибкой")
    return result.stdout


def registry_exists(psql: str) -> bool:
    answer = run_sql(f"select to_regclass('public.{REGISTRY}') is not null;", psql)
    return answer.strip() == "t"


def applied_versions(psql: str) -> dict[int, str]:
    answer = run_sql(f"select version, checksum from {REGISTRY} order by version;", psql)
    applied: dict[int, str] = {}
    for line in answer.splitlines():
        if not line.strip():
            continue
        version, _, checksum = line.partition("|")
        applied[int(version)] = checksum.strip()
    return applied


def apply_one(migration: model.Migration, psql: str) -> None:
    """Миграция и запись о ней — в одной транзакции. Иначе одно без другого."""
    sql = (
        f"{migration.sql}\n"
        f"insert into {REGISTRY} (version, checksum) "
        f"values ({migration.version}, '{migration.checksum}');\n"
    )
    run_sql(sql, psql, single_transaction=True)


def mismatches(migrations: Sequence[model.Migration], applied: dict[int, str]) -> list[str]:
    """Расхождения между реестром и файлами."""
    problems: list[str] = []
    by_version = {migration.version: migration for migration in migrations}

    for migration in migrations:
        recorded = applied.get(migration.version)
        if recorded is None:
            continue
        if recorded != migration.checksum:
            problems.append(
                f"{migration.file_name}: содержимое изменилось после применения.\n"
                f"    в реестре: {recorded}\n"
                f"    на диске:  {migration.checksum}\n"
                f"    Применённую миграцию не редактируют. Верните файл как был и "
                f"напишите новую миграцию."
            )

    for version in sorted(set(applied) - set(by_version)):
        problems.append(
            f"версия {version} применена к базе, но файла для неё нет. "
            f"Удалять применённые миграции нельзя — историю базы это не отменяет."
        )

    return problems


def command_verify(migrations: Sequence[model.Migration], psql: str) -> int:
    if not registry_exists(psql):
        print(f"реестра {REGISTRY} нет: миграции ни разу не применялись", file=sys.stderr)
        return 1

    applied = applied_versions(psql)
    problems = mismatches(migrations, applied)
    pending = [migration for migration in migrations if migration.version not in applied]

    for line in problems:
        print(line, file=sys.stderr)
    if pending:
        print("не применены: " + ", ".join(migration.file_name for migration in pending),
              file=sys.stderr)

    if problems or pending:
        return 1

    print(f"Сверено миграций: {len(migrations)}. Реестр и файлы совпадают.")
    return 0


def command_apply(migrations: Sequence[model.Migration], psql: str) -> int:
    if not migrations:
        print("миграций нет — применять нечего")
        return 0

    if registry_exists(psql):
        applied = applied_versions(psql)
        problems = mismatches(migrations, applied)
        if problems:
            # Сначала сверка, потом изменения: применять что-то поверх
            # расхождения — верный способ его закрепить.
            for line in problems:
                print(line, file=sys.stderr)
            return 1
    else:
        applied = {}
        first = migrations[0]
        if REGISTRY not in first.sql.lower():
            print(
                f"реестра {REGISTRY} нет, и первая миграция ({first.file_name}) его не заводит. "
                f"Механизм не сможет записать, что он что-то применил.",
                file=sys.stderr,
            )
            return 1

    pending = [migration for migration in migrations if migration.version not in applied]
    if not pending:
        print(f"Применять нечего: все {len(migrations)} миграций уже в реестре.")
        return 0

    for migration in pending:
        print(f"применяю {migration.file_name}")
        apply_one(migration, psql)

    print(f"Применено миграций: {len(pending)}. Всего в реестре: {len(migrations)}.")
    return 0


def command_status(migrations: Sequence[model.Migration], psql: str) -> int:
    if not registry_exists(psql):
        print(f"реестра {REGISTRY} нет: миграции ни разу не применялись")
        for migration in migrations:
            print(f"  ждёт    {migration.file_name}")
        return 0

    applied = applied_versions(psql)
    for migration in migrations:
        recorded = applied.get(migration.version)
        if recorded is None:
            state = "ждёт   "
        elif recorded != migration.checksum:
            state = "РАСХОЖД"
        else:
            state = "применена"
        print(f"  {state} {migration.file_name}  {migration.checksum[:12]}")

    for version in sorted(set(applied) - {migration.version for migration in migrations}):
        print(f"  БЕЗ ФАЙЛА версия {version}")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Применение миграций и сверка сумм.")
    parser.add_argument("command", choices=("apply", "verify", "status"))
    parser.add_argument("--dir", type=Path, default=root / "db/migrations")
    parser.add_argument("--psql", default="psql", help="чем исполнять SQL")
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

    handlers = {"apply": command_apply, "verify": command_verify, "status": command_status}
    try:
        return handlers[arguments.command](migrations, arguments.psql)
    except DatabaseError as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
