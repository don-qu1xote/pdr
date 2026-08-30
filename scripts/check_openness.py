#!/usr/bin/env python3
"""Ревизия открытости: таблицы не расходятся с деревом (PDR-DOC-08).

Две работы про одно — не оказаться запертыми. Чужое у нас
(docs/architecture/openness.md) и наши постулаты
(docs/architecture/postulates.md) описаны таблицами, а таблица, которую никто
не сверяет, через полгода описывает намерения полугодовой давности и выглядит
при этом убедительно.

Проверяется:

* у каждой строки таблицы открытости есть пометка о наборе «сервиса нет
  навсегда»: имя инстанцированного набора, «кода нет» или «не требуется».
  Пометка сверяется с деревом в обе стороны — интеграция, у которой набор
  инстанцирован, обязана быть в таблице, а строка не может обещать набор,
  которого нет. То же с реестром интеграций: появилась запись — появилась
  строка;
* полная выгрузка аккаунта содержит все обещанные части, и обещанные части
  совпадают с тем, что выгружает db/account/export.sql;
* КАЖДАЯ доменная таблица (та, у которой есть tenant_id) либо попадает в
  выгрузку, либо названа в списке «не данные аккаунта» с причиной. Молчаливо
  забыть таблицу нельзя — иначе обещание «данные ваши» станет обещанием «часть
  ваших данных ваши»;
* в таблице постулатов у каждой строки есть либо ссылка на существующий файл,
  цель сборки или прогон ctest, либо пометка «намерение». Постулат без теста —
  лозунг, и отличать одно от другого должна машина, а не память ревьюера;
* всякая ссылка в обеих таблицах ведёт туда, где что-то есть.

Запуск:
    python3 scripts/check_openness.py
    python3 scripts/check_openness.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_integrations as integrations  # noqa: E402  (после правки sys.path)
import migration_model as model  # noqa: E402  (после правки sys.path)

OPENNESS = Path("docs/architecture/openness.md")
POSTULATES = Path("docs/architecture/postulates.md")
EXPORT = Path("db/account/export.sql")
MIGRATIONS = Path("db/migrations")

TENANT_COLUMN = "tenant_id"
INTENTION = "намерение"

NO_CODE = "кода нет"
NOT_NEEDED = "не требуется"

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
CODE = re.compile(r"`([^`]+)`")
EXPORTED = re.compile(r"\bfrom\s+([a-z_][a-z0-9_]*)\s+t\b", re.I)
CONTRACT_CALL = re.compile(r"PDR_INTEGRATION_CONTRACT\(\s*(\w+)")

SCAFFOLDING = Path("libs/pdr-testing")

BUILD_FILES = ("CMakeLists.txt", "Makefile")
SKIPPED = frozenset({".git", "build", "out", "_deps", "__pycache__", "node_modules"})


class OpennessError(Exception):
    """Разбор не удался. Это отказ, а не предупреждение."""


def tables_by(text: str, header: str) -> list[tuple[int, list[str]]]:
    """Строки таблицы, у которой первый столбец назван так."""
    rows: list[tuple[int, list[str]]] = []
    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        found = TABLE_ROW.match(line.strip())
        if not found:
            inside = False
            continue
        cells = [cell.strip() for cell in found.group("cells").split("|")]
        if cells[0] == header:
            inside = True
            continue
        if not inside:
            continue
        if all(set(cell) <= {"-", ":", " "} for cell in cells):
            continue
        rows.append((number, cells))
    return rows


def build_text(root: Path) -> str:
    """Весь текст построечных файлов: в нём живут имена прогонов и целей."""
    parts: list[str] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name not in BUILD_FILES:
            continue
        if any(part in SKIPPED for part in path.parts):
            continue
        parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def instantiated(root: Path) -> set[str]:
    """Наборы, инстанцированные НАСТОЯЩИМИ интеграциями.

    Оснастка тестов не в счёт: образцовый мир, нарочно сломанный мир и пример в
    доке макроса живут в libs/pdr-testing и никуда не ходят. Они образец, а не
    интеграция, — так это и записано в docs/architecture/integrations.md.
    """
    found: set[str] = set()
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".hpp", ".cpp") or not path.is_file():
            continue
        relative = path.relative_to(root)
        if any(part in SKIPPED for part in relative.parts):
            continue
        if relative.is_relative_to(SCAFFOLDING):
            continue
        found.update(name.lower()
                     for name in CONTRACT_CALL.findall(
                         path.read_text(encoding="utf-8", errors="replace")))
    return found


def resolves(root: Path, reference: str, builds: str) -> bool:
    """Ссылка ведёт в файл, в цель сборки или в прогон ctest."""
    if (root / reference).exists():
        return True
    if reference in builds:
        return True
    tail = reference.rsplit(".", 1)[-1]
    return bool(tail) and tail in builds


def domain_tables(root: Path) -> tuple[dict[str, model.Table], list[str]]:
    """Таблицы с арендатором: их и обещает выгрузка."""
    directory = root / MIGRATIONS
    if not directory.is_dir():
        return {}, []

    found: dict[str, model.Table] = {}
    try:
        migrations = model.load(directory)
    except model.MigrationError as error:
        return {}, [str(error)]

    merged = model.merged_tables(migrations)
    for migration in migrations:
        for table in (merged.get(item.name, item) for item in migration.tables):
            if any(column.name == TENANT_COLUMN for column in table.columns):
                found[table.name] = table
    return found, []


def check_openness_table(root: Path, text: str) -> list[str]:
    """Пометка о наборе «сервиса нет навсегда» сверяется с деревом в обе стороны."""
    violations: list[str] = []
    rows = tables_by(text, "Зависимость")
    if not rows:
        return [f"{OPENNESS}: таблицы открытости не нашлось"]

    marks: dict[str, int] = {}
    for number, cells in rows:
        if len(cells) < 5:
            violations.append(
                f"{OPENNESS}:{number}: в строке меньше пяти частей — зависимость, вид, "
                f"альтернатива, что при исчезновении, набор «сервиса нет навсегда»"
            )
            continue
        found = CODE.findall(cells[4])
        if not found:
            violations.append(
                f"{OPENNESS}:{number}: у «{cells[0]}» не сказано, чем проверяется исчезновение "
                f"сервиса. Бывает имя инстанцированного набора, «{NO_CODE}» или «{NOT_NEEDED}»"
            )
            continue
        for mark in found:
            marks[mark.lower()] = number

    named = {mark for mark in marks if mark not in (NO_CODE, NOT_NEEDED)}
    contracts = instantiated(root)

    registry = root / integrations.REGISTRY
    listed = set()
    if registry.is_file():
        listed = {row["name"].lower()
                  for row in integrations.parse_registry(registry.read_text(encoding="utf-8"))}

    for mark in sorted(named - contracts):
        violations.append(
            f"{OPENNESS}:{marks[mark]}: строка обещает набор «{mark}», а он нигде не "
            f"инстанцирован. PDR_INTEGRATION_CONTRACT({mark}, ...) в дереве нет"
        )
    for prefix in sorted(contracts - named):
        violations.append(
            f"{OPENNESS}: набор «сервиса нет навсегда» инстанцирован для «{prefix}», а строки "
            f"в таблице открытости нет. Ревизия, не знающая про живую интеграцию, — не ревизия"
        )
    for name in sorted(listed - named):
        violations.append(
            f"{OPENNESS}: интеграция «{name}» есть в реестре ({integrations.REGISTRY}), а в "
            f"таблице открытости её нет либо она помечена как отсутствующая"
        )

    return violations


def check_account_export(root: Path, text: str) -> list[str]:
    """Выгрузка, обещанные части и схема сходятся втроём."""
    violations: list[str] = []

    promised = {}
    for number, cells in tables_by(text, "Часть"):
        for name in CODE.findall(cells[0]):
            promised[name] = number

    excluded = {}
    for number, cells in tables_by(text, "Таблица"):
        names = CODE.findall(cells[0])
        for name in names:
            excluded[name] = number
        if len(cells) < 2 or not cells[1]:
            violations.append(
                f"{OPENNESS}:{number}: у таблицы «{', '.join(names) or cells[0]}» не названа "
                f"причина, по которой её нет в выгрузке. Исключение без причины — это забытая "
                f"таблица"
            )

    export = root / EXPORT
    if not export.is_file():
        return violations + [
            f"{EXPORT}: полной выгрузки аккаунта нет. Обещание «данные ваши» подтверждается "
            f"работающей командой, а не абзацем"
        ]

    exported = set(EXPORTED.findall(export.read_text(encoding="utf-8")))
    tables, problems = domain_tables(root)
    violations.extend(problems)

    for name in sorted(exported - set(promised)):
        violations.append(
            f"{OPENNESS}: выгрузка отдаёт «{name}», а в списке частей его нет. Человек обязан "
            f"видеть состав выгрузки заранее"
        )
    for name in sorted(set(promised) - exported):
        violations.append(
            f"{OPENNESS}:{promised[name]}: часть «{name}» обещана, а выгрузка её не отдаёт "
            f"({EXPORT})"
        )
    for name in sorted(exported - set(tables)):
        violations.append(
            f"{EXPORT}: выгружается «{name}», а такой доменной таблицы в миграциях нет"
        )
    for name in sorted(set(tables) - exported - set(excluded)):
        violations.append(
            f"{EXPORT}: таблица «{name}» не попала ни в выгрузку, ни в список «не данные "
            f"аккаунта». Обещание «данные ваши» не терпит молча забытой таблицы"
        )
    for name in sorted(set(excluded) & exported):
        violations.append(
            f"{OPENNESS}: «{name}» одновременно выгружается и объявлена не данными аккаунта. "
            f"Одно из двух"
        )

    return violations


def check_postulates(root: Path, text: str, builds: str) -> list[str]:
    """Постулат без теста — лозунг, и это должно быть видно машине."""
    violations: list[str] = []
    rows = tables_by(text, "Постулат")
    if not rows:
        return [f"{POSTULATES}: таблицы постулатов не нашлось"]

    for number, cells in rows:
        if len(cells) < 3:
            violations.append(
                f"{POSTULATES}:{number}: в строке меньше трёх частей — постулат, где обеспечен, "
                f"каким тестом"
            )
            continue

        postulate, where, proof = cells[0], cells[1], cells[2]
        for reference in CODE.findall(where) + CODE.findall(proof):
            if not resolves(root, reference, builds):
                violations.append(
                    f"{POSTULATES}:{number}: ссылка «{reference}» никуда не ведёт: ни файла, ни "
                    f"цели сборки, ни прогона с таким именем"
                )

        if INTENTION in proof.lower():
            if len(proof.strip()) <= len(INTENTION) + 2:
                violations.append(
                    f"{POSTULATES}:{number}: у постулата «{postulate}» стоит «{INTENTION}» без "
                    f"причины. Причина — единственное, что отличает признанный долг от отписки"
                )
            continue

        if not CODE.findall(proof):
            violations.append(
                f"{POSTULATES}:{number}: у постулата «{postulate}» нет ни ссылки на тест, ни "
                f"пометки «{INTENTION}». Постулат без теста — лозунг"
            )

    return violations


def check(root: Path) -> tuple[list[str], int, int]:
    openness = root / OPENNESS
    postulates = root / POSTULATES

    if not openness.is_file():
        return [f"{OPENNESS}: ревизии открытости нет"], 0, 0
    if not postulates.is_file():
        return [f"{POSTULATES}: сверки постулатов нет"], 0, 0

    open_text = openness.read_text(encoding="utf-8")
    postulate_text = postulates.read_text(encoding="utf-8")
    builds = build_text(root)

    violations = [
        *check_openness_table(root, open_text),
        *check_account_export(root, open_text),
        *check_postulates(root, postulate_text, builds),
    ]

    return violations, len(tables_by(open_text, "Зависимость")), len(
        tables_by(postulate_text, "Постулат")
    )


SELFTEST_OPENNESS = """# Ревизия для самопроверки

| Зависимость | Вид | Альтернатива | Что при исчезновении | Набор «сервиса нет навсегда» |
| --- | --- | --- | --- | --- |
| Видео | открытый | форк | ничего | `кода нет` |
| Календари | стандарт | нет зависимости | нечему исчезать | `не требуется` |
| Чеки | проприетарный | нет | репетитор без чека | `receipts` |
| Платежи | проприетарный | четыре | оплата встаёт | |

| Часть | Что в ней |
| --- | --- |
| `identity_tenant` | сам арендатор |
| `identity_person` | люди |
| `identity_ghost` | часть, которой выгрузка не отдаёт |

| Таблица | Почему это не данные аккаунта |
| --- | --- |
| `jobs_effect` | след механизма |
| `identity_person` | |
"""

SELFTEST_POSTULATES = """# Постулаты для самопроверки

| Постулат | Где обеспечен | Каким тестом |
| --- | --- | --- |
| Изоляция структурная | `db/migrations/V002__init.sql` | `scripts/check_rls.py` |
| Часы — порт | `libs/pdr-core/src/application/ports/clock.hpp` | `unit` |
| Данные карт не ходят через нас | нигде: платежей нет | намерение — вместе с областью BILL |
| Лозунг без теста | `Makefile` | скоро проверим |
| Ссылка в никуда | `libs/pdr-core/src/core/missing.hpp` | `unit` |
| Отписка | `Makefile` | намерение |
"""

SELFTEST_EXPORT = """set role pdr_app;

select jsonb_build_object(
    'identity_tenant',
    (select jsonb_agg(to_jsonb(t)) from identity_tenant t),
    'identity_person',
    (select jsonb_agg(to_jsonb(t)) from identity_person t),
    'observability_product_event',
    (select jsonb_agg(to_jsonb(t)) from observability_product_event t)
);
"""

SELFTEST_MIGRATION = """create table identity_tenant (
    tenant_id uuid not null,
    name      text not null,
    constraint identity_tenant_pk primary key (tenant_id)
);

create table identity_person (
    tenant_id uuid not null,
    id        uuid not null,
    constraint identity_person_pk primary key (tenant_id, id)
);

create table jobs_effect (
    tenant_id uuid not null,
    job       text not null,
    constraint jobs_effect_pk primary key (tenant_id, job)
);

create table scheduling_lesson (
    tenant_id uuid not null,
    id        uuid not null,
    constraint scheduling_lesson_pk primary key (tenant_id, id)
);
"""

SELFTEST_FILES = {
    "docs/architecture/openness.md": SELFTEST_OPENNESS,
    "docs/architecture/postulates.md": SELFTEST_POSTULATES,
    "db/account/export.sql": SELFTEST_EXPORT,
    "db/migrations/V002__init.sql": SELFTEST_MIGRATION,
    "Makefile": "test:\n\tpython3 scripts/check_openness.py\n",
    "CMakeLists.txt": "add_test(NAME unit COMMAND pdr_unit_tests)\n",
    "scripts/check_rls.py": "#!/usr/bin/env python3\n",
    "libs/pdr-core/src/application/ports/clock.hpp": "#pragma once\n",
    "db/migrations/V001__schema_version.sql": "create table schema_version (\n"
                                             "    version integer not null,\n"
                                             "    constraint schema_version_pk primary key (version)\n"
                                             ");\n",
}

SELFTEST_EXPECTED = (
    ("Платежи", "не сказано, чем проверяется"),
    ("receipts", "нигде не инстанцирован"),
    ("identity_ghost", "обещана, а выгрузка её не отдаёт"),
    ("observability_product_event", "в списке частей его нет"),
    ("scheduling_lesson", "ни в выгрузку, ни в список"),
    ("identity_person", "одновременно выгружается и объявлена"),
    ("identity_person", "не названа причина"),
    ("Лозунг без теста", "ни пометки"),
    ("missing.hpp", "никуда не ведёт"),
    ("Отписка", "без причины"),
)

SELFTEST_CLEAN = ("Видео", "Календари", "Изоляция структурная", "jobs_effect", "clock.hpp")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, openness_rows, postulate_rows = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильное объявлено нарушением: {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if openness_rows != 4 or postulate_rows != 6:
            print(f"самопроверка: разобрано {openness_rows} строк открытости и "
                  f"{postulate_rows} постулатов вместо четырёх и шести", file=sys.stderr)
            return 1

        (root / EXPORT).unlink()
        gone, _, _ = check(root)
        if not any("работающей командой" in line for line in gone):
            print("самопроверка: пропажа выгрузки прошла мимо проверки", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 1} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Ревизия открытости (PDR-DOC-08).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, openness_rows, postulate_rows = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Ревизия — {OPENNESS}", file=sys.stderr)
        return 1

    print(f"Внешних зависимостей в ревизии: {openness_rows}, постулатов сверено: "
          f"{postulate_rows}. Таблицы и дерево сходятся.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
