#!/usr/bin/env python3
"""Проверка изоляции арендаторов: доменная таблица без RLS роняет сборку.

Изоляция у нас структурная (ADR-0003): чужую строку не показывает база, а не
аккуратно написанный запрос. Держится это на трёх вещах сразу, и любая из них
поодиночке ничего не стоит:

* `enable row level security` — политика начинает действовать;
* `force row level security` — она действует и на владельца таблицы, так что
  «служебного запроса в обход» не существует;
* политика, сравнивающая `tenant_id` с параметром сессии `pdr.tenant_id`.

Забыть третье при первых двух — значит закрыть таблицу совсем. Забыть первое
при третьем — значит открыть её всем: политика лежит рядом и не применяется.
Поэтому проверяется весь набор, а не наличие слова «policy» в файле.

ЕДИНСТВЕННОЕ ИСКЛЮЧЕНИЕ — РАЗБОР ОЧЕРЕДИ, и оно списком. Фоновому отправщику
нужны строки всех практик сразу: практики он не выбирает, он спрашивает «чему
настал срок». Такой таблице разрешена ВТОРАЯ политика, и разрешена в закрытой
форме: имя `<таблица>_dispatch`, тело слово в слово, отдельный параметр сессии
`pdr.dispatch`. Отдельный затем, что забытый `pdr.tenant_id` обязан остаться
пустым ответом, а не ключом ко всему: чтобы увидеть очередь целиком, разбор
объявляют ЯВНО. Список таблиц ниже, решение — docs/adr/0022-outbox-dispatch-across-tenants.md.

Проверяется и вторая сторона: запрос, которым арендатор объявляется базе, обязан
объявлять ТОТ ЖЕ параметр и объявлять его локально для транзакции. Опечатка в
имени параметра — это пустые ответы во всей системе, а `false` вместо `true` —
арендатор, оставшийся на соединении после возврата в пул. С PDR-DB-05 этот
запрос лежит файлом (`db/sql/core/declare_tenant.sql`), а не строкой в адаптере,
и проверяется он там же, где написан.

Нарушение печатается как <файл>:<строка> и даёт код возврата 1.

Запуск:
    python3 scripts/check_rls.py
    python3 scripts/check_rls.py --selftest
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

PARAMETER = "pdr.tenant_id"

TENANT_COLUMN = "tenant_id"

ADAPTER = Path("db/sql/core/declare_tenant.sql")

DISPATCH_PARAMETER = "pdr.dispatch"

DISPATCH_ADAPTER = Path("db/sql/notifications/notifications_declare_dispatch.sql")

DISPATCH_SUFFIX = "_dispatch"

CROSS_TENANT_TABLES = {
    "notifications_outbox": "очередь на отправку разбирает фоновый отправщик: у него нет "
                            "ни запроса, ни человека, ни практики, а список практик лежит "
                            "под той же политикой и взять его неоткуда",
}
"""Таблицы, которые читают сквозь арендаторов. СПИСОК ЗАКРЫТ.

Он же и обязательство: таблица отсюда обязана иметь политику разбора, а политика
разбора — быть на таблице отсюда. Иначе список превращается в способ открыть
любую таблицу, дописав к ней политику с подходящим именем.
"""

DISPATCH_BODY = ("using (current_setting('pdr.dispatch', true) = 'on') "
                 "with check (current_setting('pdr.dispatch', true) = 'on')")
"""Тело политики разбора — СЛОВО В СЛОВО, а не «примерно такое».

Свобода формулировки здесь не нужна никому, а стоит дорого: `using (true)`,
`using (current_setting(...) is not null)` и `using (... <> 'off')` открывают
таблицу целиком и выглядят похоже на правильное.
"""

SET_DISPATCH = re.compile(r"set_config\(\s*'([^']*)'\s*,\s*'([^']*)'\s*,\s*(true|false)\s*\)")

SET_CONFIG = re.compile(r"set_config\(\s*'([^']*)'\s*,\s*\$1\s*,\s*(true|false)\s*\)")

DISABLING = {"disable", "no force"}


def _meta_columns_are_locked(table: str, place: str, definition) -> list[str]:
    """Состав колонок мета-таблицы заперт списком.

    Мета-таблица — единственное место, где строка пересекает границу
    арендатора. Пока в ней лежат отпечаток и идентификатор, пересекать нечему;
    новая колонка — это данные, которые видны всем практикам сразу, и заводить
    её молча нельзя. Отсюда же берётся проверка «общего числа готовности вообще
    не существует нигде»: такому числу пришлось бы завестись здесь.
    """
    allowed = model.META_TABLE_COLUMNS.get(table)
    if allowed is None:
        return [
            f"{place}: таблица {table} объявлена мета-таблицей, а состав её колонок нигде "
            f"не заперт. Допишите его в model.META_TABLE_COLUMNS — иначе в таблицу без "
            f"построчной защиты однажды добавят колонку, видную всем арендаторам сразу"
        ]

    found = {column.name for column in definition.columns}
    extra = sorted(found - allowed)
    if extra:
        return [
            f"{place}: в мета-таблице {table} завелись колонки {', '.join(extra)}, которых "
            f"нет в разрешённом составе. Эта таблица без построчной защиты: всё, что в ней "
            f"лежит, видно всем практикам сразу"
        ]
    return []


def _dispatch_is_declared(
    table: str, place: str, dispatching: list[tuple[str, model.Policy]]
) -> list[str]:
    """Политика разбора есть ровно там, где она названа, и ровно в той форме.

    Проверяется в обе стороны. Политика разбора на таблице вне списка — это
    открытая таблица, о которой никто не договаривался. Таблица из списка без
    такой политики — устаревший список: строка в нём читается как «сюда ходят
    сквозь арендаторов», а ходить уже нечем.
    """
    reason = CROSS_TENANT_TABLES.get(table)

    if reason is None:
        return [
            f"{_place(source, policy.line)}: политика {policy.name} читает таблицу {table} "
            f"сквозь арендаторов, а таблицы нет в CROSS_TENANT_TABLES. Список закрыт: "
            f"вторая политика на доменной таблице — это отдельное решение с ADR, а не "
            f"строчка в миграции"
            for source, policy in dispatching
        ]

    if not dispatching:
        return [
            f"{place}: таблица {table} названа в CROSS_TENANT_TABLES ({reason}), а политики "
            f"{table}{DISPATCH_SUFFIX} у неё нет. Либо заведите её, либо уберите таблицу из "
            f"списка: список, который ничего не описывает, читают и верят ему"
        ]

    if len(dispatching) > 1:
        source, policy = dispatching[1]
        return [
            f"{_place(source, policy.line)}: у таблицы {table} вторая политика разбора. "
            f"Дверь одна: две складываются по «или», и что именно открыто, "
            f"перестаёт быть видно"
        ]

    source, policy = dispatching[0]
    spot = _place(source, policy.line)
    violations: list[str] = []
    if policy.name != f"{table}{DISPATCH_SUFFIX}":
        violations.append(
            f"{spot}: политика разбора названа {policy.name}, а обязана — "
            f"{table}{DISPATCH_SUFFIX}: по имени её находят в схеме, не читая тела"
        )
    if " ".join(policy.body.lower().split()) != DISPATCH_BODY:
        violations.append(
            f"{spot}: тело политики {policy.name} не совпадает со словом в слово "
            f"разрешённым. Разрешено ровно «{DISPATCH_BODY}»: любая другая форма "
            f"открывает таблицу целиком и выглядит при этом похоже"
        )
    return violations


def _place(source: str, line: int) -> str:
    return f"{source}:{line}"


def _has_tenant(table: model.Table) -> bool:
    return any(column.name == TENANT_COLUMN for column in table.columns)


def check_migrations(migrations: Sequence[tuple[str, model.Migration]]) -> tuple[list[str], int]:
    """Нарушения изоляции и число проверенных доменных таблиц."""
    violations: list[str] = []

    merged = model.merged_tables([migration for _, migration in migrations])

    created: dict[str, tuple[str, model.Table]] = {}
    enabled: dict[str, str] = {}
    forced: dict[str, str] = {}
    policies: dict[str, list[tuple[str, model.Policy]]] = {}

    for source, migration in migrations:
        for table in (merged.get(item.name, item) for item in migration.tables):
            created[table.name] = (_place(source, table.line), table)

        for change in migration.row_security:
            place = _place(source, change.line)
            if change.action in DISABLING:
                violations.append(
                    f"{place}: у таблицы {change.table} выключают построчную защиту "
                    f"(«{change.action} row level security»). Служебных запросов в обход "
                    f"политики не бывает: запросу, которому нужен другой арендатор, "
                    f"объявляют другого арендатора"
                )
                continue
            if change.action == "enable":
                enabled[change.table] = place
            elif change.action == "force":
                forced[change.table] = place

        for policy in migration.policies:
            policies.setdefault(policy.table, []).append((source, policy))

    for table, place in sorted({**enabled, **forced}.items()):
        if table not in created:
            violations.append(
                f"{place}: построчная защита включается на таблице {table}, "
                f"которой не заводит ни одна миграция"
            )
    for table, items in sorted(policies.items()):
        if table in created:
            continue
        source, policy = items[0]
        violations.append(
            f"{_place(source, policy.line)}: политика {policy.name} заведена на таблице "
            f"{table}, которой не заводит ни одна миграция"
        )

    checked = 0
    for table, (place, definition) in sorted(created.items()):
        if table in model.META_TABLES and not _has_tenant(definition):
            violations.extend(_meta_columns_are_locked(table, place, definition))
            continue
        checked += 1

        if table not in enabled:
            violations.append(
                f"{place}: таблица {table} заведена без построчной защиты. "
                f"«alter table {table} enable row level security» — в той же миграции, "
                f"вместе с политикой: доменная таблица без RLS видна всем арендаторам"
            )
        if table not in forced:
            violations.append(
                f"{place}: у таблицы {table} нет «force row level security». Без него "
                f"политику не соблюдает владелец таблицы, а миграции и служебные "
                f"скрипты ходят именно под ним"
            )

        table_policies = policies.get(table, [])
        if not table_policies:
            violations.append(
                f"{place}: у таблицы {table} нет ни одной политики. С включённой "
                f"защитой она окажется закрыта совсем, включая её владельца, без "
                f"защиты — открыта всем. Изоляция — ни то ни другое"
            )

        dispatching = [item for item in table_policies if item[1].name.endswith(DISPATCH_SUFFIX)]
        violations.extend(_dispatch_is_declared(table, place, dispatching))

        for source, policy in table_policies:
            if policy.name.endswith(DISPATCH_SUFFIX):
                continue
            spot = _place(source, policy.line)
            body = policy.body.lower()
            if f"current_setting('{PARAMETER}'" not in body:
                violations.append(
                    f"{spot}: политика {policy.name} не смотрит на параметр сессии "
                    f"«{PARAMETER}». Политики складываются по «или»: одна такая "
                    f"открывает таблицу {table} целиком"
                )
            if TENANT_COLUMN not in body:
                violations.append(
                    f"{spot}: политика {policy.name} не сравнивает {TENANT_COLUMN} "
                    f"с арендатором сессии"
                )
            if "with check" not in body:
                violations.append(
                    f"{spot}: у политики {policy.name} нет «with check». Без него "
                    f"арендатор вставит в {table} строку с чужим {TENANT_COLUMN} — "
                    f"и сам её не увидит"
                )

    return violations, checked


def check_adapter(path: Path, source: str) -> list[str]:
    """Запрос объявляет тот же параметр и объявляет его на время транзакции."""
    if not path.is_file():
        return [
            f"{source}: файла нет. Политики есть, а объявить арендатора базе некому — "
            f"любой запрос вернёт пусто"
        ]

    text = path.read_text(encoding="utf-8")
    found = list(SET_CONFIG.finditer(text))
    if not found:
        return [
            f"{source}: запрос не вызывает set_config('{PARAMETER}', $1, true). "
            f"Арендатор объявляется базе здесь и только здесь"
        ]

    violations: list[str] = []
    for match in found:
        line = text[: match.start()].count("\n") + 1
        name, local = match.group(1), match.group(2)
        if name != PARAMETER:
            violations.append(
                f"{source}:{line}: запрос объявляет параметр «{name}», а политики "
                f"смотрят на «{PARAMETER}». Разъезд имён — это пустые ответы везде"
            )
        if local != "true":
            violations.append(
                f"{source}:{line}: параметр объявлен с «{local}» вместо «true». "
                f"Объявление обязано жить ровно до конца транзакции: иначе арендатор "
                f"останется на соединении и уедет в пул к следующему запросу"
            )
    return violations


def check_dispatch_adapter(path: Path, source: str) -> list[str]:
    """Разбор объявляется тем же способом, что и арендатор: явно и на транзакцию.

    Файл обязан существовать, пока в схеме есть политика разбора: политика без
    объявления — открытая дверь, в которую никто не входит, и заметить её
    некому. `false` вместо `true` здесь хуже, чем у арендатора: объявление
    уехало бы в пул, и следующий запрос увидел бы очередь всех практик.
    """
    if not path.is_file():
        return [
            f"{source}: файла нет. Политика разбора в схеме есть, а объявить разбор "
            f"базе некому — отправщик увидит пусто и замолчит навсегда"
        ]

    text = path.read_text(encoding="utf-8")
    found = list(SET_DISPATCH.finditer(text))
    if not found:
        return [
            f"{source}: запрос не вызывает set_config('{DISPATCH_PARAMETER}', 'on', true). "
            f"Разбор объявляется здесь и только здесь"
        ]

    violations: list[str] = []
    for match in found:
        line = text[: match.start()].count("\n") + 1
        name, value, local = match.group(1), match.group(2), match.group(3)
        if name != DISPATCH_PARAMETER:
            violations.append(
                f"{source}:{line}: запрос объявляет параметр «{name}», а политика разбора "
                f"смотрит на «{DISPATCH_PARAMETER}». Разъезд имён — это молчащий отправщик"
            )
        if value != "on":
            violations.append(
                f"{source}:{line}: объявлено значение «{value}», а политика сравнивает с «on»"
            )
        if local != "true":
            violations.append(
                f"{source}:{line}: разбор объявлен с «{local}» вместо «true». Объявление "
                f"обязано жить ровно до конца транзакции: иначе соединение вернётся в пул "
                f"с правом видеть очередь всех практик"
            )
    return violations


def check(directory: Path, root: Path, adapter: Path) -> tuple[list[str], int]:
    """Нарушения изоляции по всей схеме и число проверенных доменных таблиц.

    Объявление разбора спрашивается тогда, когда есть что разбирать: пока
    таблицы из закрытого списка нет в схеме, нет и файла запроса — и это не
    нарушение, а порядок вещей у самопроверки и у пустого дерева.
    """
    try:
        loaded = model.load(directory)
    except model.MigrationError as error:
        return ([str(error)], 0)

    pairs: list[tuple[str, model.Migration]] = []
    for migration in loaded:
        try:
            source = str(migration.path.relative_to(root))
        except ValueError:
            source = migration.path.name
        problems = model.unsupported(migration.sql, source)
        if problems:
            return (problems, 0)
        pairs.append((source, migration))

    violations, checked = check_migrations(pairs)

    try:
        adapter_source = str(adapter.relative_to(root))
    except ValueError:
        adapter_source = str(adapter)
    violations.extend(check_adapter(adapter, adapter_source))

    created = {table.name for _, migration in pairs for table in migration.tables}
    if created & set(CROSS_TENANT_TABLES):
        violations.extend(check_dispatch_adapter(root / DISPATCH_ADAPTER, str(DISPATCH_ADAPTER)))

    return violations, checked


GOOD_TABLE = """
create table identity_person (
    tenant_id uuid not null,
    id        uuid not null,
    constraint identity_person_pk primary key (tenant_id, id)
);

alter table identity_person enable row level security;
alter table identity_person force row level security;

create policy identity_person_isolation on identity_person
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
"""

GOOD_ADAPTER = """
-- Объявление арендатора: тот же параметр, что в политиках, и на транзакцию.
SELECT set_config('pdr.tenant_id', $1, true)
"""

GOOD_DISPATCH_ADAPTER = """
-- Объявление разбора: отдельный параметр и тоже на транзакцию.
SELECT set_config('pdr.dispatch', 'on', true)
"""

GOOD_OUTBOX = """
create table notifications_outbox (
    tenant_id uuid not null,
    id        uuid not null,
    constraint notifications_outbox_pk primary key (tenant_id, id)
);

alter table notifications_outbox enable row level security;
alter table notifications_outbox force row level security;

create policy notifications_outbox_isolation on notifications_outbox
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy notifications_outbox_dispatch on notifications_outbox
    using (current_setting('pdr.dispatch', true) = 'on')
    with check (current_setting('pdr.dispatch', true) = 'on');
"""

OUTBOX_WITHOUT_DISPATCH = GOOD_OUTBOX.split("create policy notifications_outbox_dispatch")[0]

SELFTEST_FILES = {
    "V001__good.sql": GOOD_TABLE,
    "V002__no_rls.sql": """
create table scheduling_slot (
    tenant_id uuid not null,
    id        uuid not null
);
""",
    "V003__not_forced.sql": """
create table scheduling_lesson (
    tenant_id uuid not null,
    id        uuid not null
);
alter table scheduling_lesson enable row level security;
create policy scheduling_lesson_isolation on scheduling_lesson
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
""",
    "V004__open_policy.sql": """
create table billing_invoice (
    tenant_id uuid not null,
    id        uuid not null
);
alter table billing_invoice enable row level security;
alter table billing_invoice force row level security;
create policy billing_invoice_isolation on billing_invoice
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
create policy billing_invoice_reports on billing_invoice
    using (true) with check (true);
""",
    "V005__no_with_check.sql": """
create table notes_note (
    tenant_id uuid not null,
    id        uuid not null
);
alter table notes_note enable row level security;
alter table notes_note force row level security;
create policy notes_note_isolation on notes_note
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
""",
    "V006__disabled.sql": """
alter table identity_person disable row level security;
""",
    "V007__typo.sql": """
create policy identity_persons_isolation on identity_persons
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
""",
    "V008__meta_with_tenant.sql": """
create table jobs_lock (
    key       text not null,
    tenant_id uuid not null
);
""",
    "V009__meta_plain.sql": """
create table jobs_run (
    job        text        not null,
    started_at timestamptz not null
);
""",
    "V010__meta_grew.sql": """
create table identity_account (
    id           uuid     not null,
    email_digest char(64) not null,
    readiness    integer  not null
);
""",
    "V011__dispatch_uninvited.sql": """
create table chat_thread (
    tenant_id uuid not null,
    id        uuid not null
);
alter table chat_thread enable row level security;
alter table chat_thread force row level security;
create policy chat_thread_isolation on chat_thread
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
create policy chat_thread_dispatch on chat_thread
    using (current_setting('pdr.dispatch', true) = 'on')
    with check (current_setting('pdr.dispatch', true) = 'on');
""",
    "V012__dispatch_loose.sql": """
create table notifications_outbox (
    tenant_id uuid not null,
    id        uuid not null
);
alter table notifications_outbox enable row level security;
alter table notifications_outbox force row level security;
create policy notifications_outbox_isolation on notifications_outbox
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
create policy notifications_outbox_dispatch on notifications_outbox
    using (current_setting('pdr.dispatch', true) is not null)
    with check (true);
""",
}

SELFTEST_EXPECTED = {
    ("V002__no_rls.sql", "без построчной защиты"),
    ("V003__not_forced.sql", "force row level security"),
    ("V004__open_policy.sql", "не смотрит на параметр сессии"),
    ("V005__no_with_check.sql", "with check"),
    ("V006__disabled.sql", "выключают построчную защиту"),
    ("V007__typo.sql", "не заводит ни одна миграция"),
    ("V008__meta_with_tenant.sql", "без построчной защиты"),
    ("V010__meta_grew.sql", "которых нет в разрешённом составе"),
    ("V011__dispatch_uninvited.sql", "нет в CROSS_TENANT_TABLES"),
    ("V012__dispatch_loose.sql", "не совпадает со словом в слово"),
}


def selftest() -> int:
    """Отрицательные случаи: проверка обязана падать и называть место.

    Правильная таблица из закрытого списка проверяется отдельным деревом: одно
    имя таблицы в двух вариантах в одном дереве не уживается, а проверить надо
    оба — и годный разбор, и негодный.
    """
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        migrations = root / "db" / "migrations"
        migrations.mkdir(parents=True)
        adapter = root / ADAPTER
        adapter.parent.mkdir(parents=True)
        adapter.write_text(GOOD_ADAPTER, encoding="utf-8")

        (migrations / "V001__good.sql").write_text(GOOD_TABLE, encoding="utf-8")
        violations, checked = check(migrations, root, adapter)
        if violations or checked != 1:
            print(f"самопроверка: чистый случай не прошёл: {violations}", file=sys.stderr)
            return 1

        for name, text in SELFTEST_FILES.items():
            (migrations / name).write_text(text, encoding="utf-8")

        violations, checked = check(migrations, root, adapter)
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
        if any("V009__meta_plain.sql" in line for line in violations):
            print("самопроверка: метатаблица без арендатора объявлена нарушением", file=sys.stderr)
            for line in violations:
                print("    " + line, file=sys.stderr)
            return 1

        adapter.write_text(
            GOOD_ADAPTER.replace("pdr.tenant_id", "pdr.tenant").replace("$1, true", "$1, false"),
            encoding="utf-8",
        )
        broken = check_adapter(adapter, str(ADAPTER))
        if not any("смотрят на" in line for line in broken):
            print("самопроверка: не пойман разъезд имени параметра", file=sys.stderr)
            return 1
        if not any("уедет в пул" in line for line in broken):
            print("самопроверка: не поймано объявление мимо транзакции", file=sys.stderr)
            return 1

        adapter.unlink()
        if not any("некому" in line for line in check_adapter(adapter, str(ADAPTER))):
            print("самопроверка: не поймано отсутствие файла запроса", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        migrations = root / "db" / "migrations"
        migrations.mkdir(parents=True)
        adapter = root / ADAPTER
        adapter.parent.mkdir(parents=True)
        adapter.write_text(GOOD_ADAPTER, encoding="utf-8")
        dispatch = root / DISPATCH_ADAPTER
        dispatch.parent.mkdir(parents=True)
        dispatch.write_text(GOOD_DISPATCH_ADAPTER, encoding="utf-8")

        (migrations / "V001__outbox.sql").write_text(GOOD_OUTBOX, encoding="utf-8")
        violations, _ = check(migrations, root, adapter)
        if violations:
            print(f"самопроверка: правильный разбор объявлен нарушением: {violations}",
                  file=sys.stderr)
            return 1

        dispatch.unlink()
        violations, _ = check(migrations, root, adapter)
        if not any("объявить разбор" in line for line in violations):
            print("самопроверка: не поймано отсутствие объявления разбора", file=sys.stderr)
            return 1

        dispatch.write_text(GOOD_DISPATCH_ADAPTER.replace("'on', true", "'on', false"),
                            encoding="utf-8")
        violations, _ = check(migrations, root, adapter)
        if not any("вернётся в пул" in line for line in violations):
            print("самопроверка: не поймано объявление разбора мимо транзакции",
                  file=sys.stderr)
            return 1

        dispatch.write_text(GOOD_DISPATCH_ADAPTER, encoding="utf-8")
        (migrations / "V001__outbox.sql").write_text(OUTBOX_WITHOUT_DISPATCH, encoding="utf-8")
        violations, _ = check(migrations, root, adapter)
        if not any("у неё нет" in line for line in violations):
            print("самопроверка: не пойман устаревший список CROSS_TENANT_TABLES",
                  file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 6} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Изоляция арендаторов: RLS на каждой таблице.")
    parser.add_argument("--dir", type=Path, default=root / "db/migrations")
    parser.add_argument("--adapter", type=Path, default=root / ADAPTER)
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, checked = check(arguments.dir, root, arguments.adapter)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Как устроена изоляция — "
              f"docs/architecture/tenancy.md", file=sys.stderr)
        return 1

    print(f"Доменных таблиц проверено: {checked}. У каждой включена и форсирована "
          f"построчная защита, политика смотрит на «{PARAMETER}».")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
