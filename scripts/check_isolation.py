#!/usr/bin/env python3
"""Изоляция арендаторов на ЖИВОЙ базе: то, ради чего писалась схема.

scripts/check_rls.py читает миграции и отвечает на вопрос «написано ли
правильно». Здесь спрашивается другое: «работает ли». Между этими вопросами
помещается всё, что делает утечку возможной, — роль оказалась суперпользователем,
политику отключили руками, параметр сессии называется иначе, чем думает адаптер.

Главный случай — первый: строка одного арендатора не видна под другим НИ ОДНИМ
запросом, включая `select` без `where`. Остальные ровно про то же с разных
сторон: без параметра сессии не видно ничего, чужую строку не вставить, защиту
не выключить.

Тест пишет в базу профиля и убирает за собой: живого продакшена у проекта нет
(ADR-0007), а на чужих данных такое не запускают.

Подключение — обычными переменными PG*, их выставляет Makefile из профиля:

    make test-isolation
    make test-isolation ENV_PROFILE=ci
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402  (после правки sys.path)

APP_ROLE = "pdr_app"
PARAMETER = "pdr.tenant_id"

TENANT_A = "0a0a0a0a-0000-4000-8000-000000000001"
TENANT_B = "0b0b0b0b-0000-4000-8000-000000000002"
GUARDIAN_A = "0a0a0a0a-0000-4000-8000-00000000a001"
STUDENT_A = "0a0a0a0a-0000-4000-8000-00000000a002"
GUARDIAN_B = "0b0b0b0b-0000-4000-8000-00000000b001"
STUDENT_B = "0b0b0b0b-0000-4000-8000-00000000b002"
LINK_A = "0a0a0a0a-0000-4000-8000-00000000c001"
LINK_B = "0b0b0b0b-0000-4000-8000-00000000c002"
ROLE_A = "0a0a0a0a-0000-4000-8000-00000000d001"
ROLE_B = "0b0b0b0b-0000-4000-8000-00000000d002"
LOG_A = "0a0a0a0a-0000-4000-8000-00000000f001"
LOG_B = "0b0b0b0b-0000-4000-8000-00000000f002"
NEW_ROW = "0c0c0c0c-0000-4000-8000-00000000e001"

TABLES = ("identity_tenant", "identity_person", "identity_role_assignment",
          "identity_guardianship", "identity_access_log")

SQLSTATE = re.compile(r"\bERROR:\s+([0-9A-Z]{5}):")


class DatabaseError(Exception):
    """База недоступна или ответила не тем, чем должна."""


class Database:
    """Тонкая обёртка над psql: каждый вызов — отдельная сессия.

    Отдельная сессия здесь не мелочь, а часть проверки: параметр `pdr.tenant_id`
    объявляется заново в каждой, и «запрос без объявления» получается сам собой,
    а не имитируется.
    """

    def __init__(self, psql: str) -> None:
        self._psql = psql

    def _run(self, sql: str) -> subprocess.CompletedProcess[str]:
        command = [self._psql, "--no-psqlrc", "-v", "ON_ERROR_STOP=1", "-qtA", "-F", "|", "-f", "-"]
        try:
            return subprocess.run(
                command, input="\\set VERBOSITY verbose\n" + sql,
                capture_output=True, text=True, check=False,
            )
        except FileNotFoundError as error:
            raise DatabaseError(f"нет {self._psql}: спрашивать базу нечем") from error

    def owner(self, sql: str) -> list[list[str]]:
        """Запрос под ролью миграций: она владеет таблицами и видит всё."""
        result = self._run(sql)
        if result.returncode != 0:
            raise DatabaseError(result.stderr.strip())
        return [line.split("|") for line in result.stdout.splitlines() if line.strip()]

    def app(self, sql: str, tenant: str | None = None) -> list[list[str]]:
        """Запрос под ролью приложения — так ходит сервис."""
        result = self._run(self._as_app(sql, tenant))
        if result.returncode != 0:
            raise DatabaseError(result.stderr.strip())
        return [line.split("|") for line in result.stdout.splitlines() if line.strip()]

    def app_text(self, sql: str, tenant: str | None = None) -> str:
        """Ответ как есть, без разбора на колонки: план в JSON — одно значение
        в несколько строк, и разбирать его по разделителю нечем."""
        result = self._run(self._as_app(sql, tenant))
        if result.returncode != 0:
            raise DatabaseError(result.stderr.strip())
        return result.stdout

    def app_refusal(self, sql: str, tenant: str | None = None) -> str:
        """Код отказа SQLSTATE. Успешный запрос здесь — сам по себе провал."""
        result = self._run(self._as_app(sql, tenant))
        if result.returncode == 0:
            return ""
        found = SQLSTATE.search(result.stderr)
        return found.group(1) if found else result.stderr.strip()

    @staticmethod
    def _as_app(sql: str, tenant: str | None) -> str:
        declare = ""
        if tenant is not None:
            declare = (
                f"do $$ begin perform set_config('{PARAMETER}', '{tenant}', false); end $$;\n"
            )
        return f"set role {APP_ROLE};\n{declare}{sql}"


def seed(database: Database) -> None:
    teardown(database)
    database.owner(f"""
insert into identity_tenant (tenant_id, name, tz) values
    ('{TENANT_A}', 'Арендатор А', 'Europe/Moscow'),
    ('{TENANT_B}', 'Арендатор Б', 'Asia/Tbilisi');
insert into identity_person (tenant_id, id, display_name, email, tz) values
    ('{TENANT_A}', '{GUARDIAN_A}', 'Опекун А', 'a-guardian@example.test', 'Europe/Moscow'),
    ('{TENANT_A}', '{STUDENT_A}',  'Ученик А', null,                      'Europe/Moscow'),
    ('{TENANT_B}', '{GUARDIAN_B}', 'Опекун Б', 'b-guardian@example.test', 'Asia/Tbilisi'),
    ('{TENANT_B}', '{STUDENT_B}',  'Ученик Б', null,                      'Asia/Tbilisi');
insert into identity_role_assignment (tenant_id, id, person_id, role) values
    ('{TENANT_A}', '{ROLE_A}', '{STUDENT_A}', 'student'),
    ('{TENANT_B}', '{ROLE_B}', '{STUDENT_B}', 'student');
insert into identity_guardianship (tenant_id, id, guardian_id, student_id) values
    ('{TENANT_A}', '{LINK_A}', '{GUARDIAN_A}', '{STUDENT_A}'),
    ('{TENANT_B}', '{LINK_B}', '{GUARDIAN_B}', '{STUDENT_B}');
insert into identity_access_log (tenant_id, id, actor_id, subject_id, resource_kind, at) values
    ('{TENANT_A}', '{LOG_A}', '{GUARDIAN_A}', '{STUDENT_A}', 'recording',   now()),
    ('{TENANT_B}', '{LOG_B}', '{GUARDIAN_B}', '{STUDENT_B}', 'transcript', now());
""")


def teardown(database: Database) -> None:
    tenants = f"('{TENANT_A}', '{TENANT_B}')"
    database.owner(f"""
delete from identity_access_log where tenant_id in {tenants};
delete from identity_guardianship where tenant_id in {tenants};
delete from identity_role_assignment where tenant_id in {tenants};
delete from identity_person where tenant_id in {tenants};
delete from identity_tenant where tenant_id in {tenants};
""")


def protection_is_on(database: Database) -> list[str]:
    """Защита включена, форсирована и снабжена политикой — у каждой таблицы.

    Проверка по каталогу, а не по файлам: миграцию могли применить не всю, а
    политику — снять руками. Заодно ловится таблица, заведённая мимо миграций.
    """
    meta = ", ".join(f"'{name}'" for name in sorted(model.META_TABLES))
    rows = database.owner(f"""
select c.relname, c.relrowsecurity, c.relforcerowsecurity,
       (select count(*) from pg_policy p where p.polrelid = c.oid)
from pg_class c join pg_namespace n on n.oid = c.relnamespace
where n.nspname = 'public' and c.relkind = 'r' and c.relname not in ({meta})
order by c.relname;
""")
    if not rows:
        return ["доменных таблиц в базе нет: проверять изоляцию не на чем"]

    problems = []
    for name, enabled, forced, policies in rows:
        if enabled != "t":
            problems.append(f"{name}: построчная защита выключена — таблицу видят все")
        if forced != "t":
            problems.append(f"{name}: защита не форсирована — владелец таблицы ходит мимо неё")
        if int(policies) == 0:
            problems.append(f"{name}: защита включена, а политики нет")
    return problems


def app_role_is_ordinary(database: Database) -> list[str]:
    """Роль приложения не суперпользователь и не обходит политику.

    Без этой проверки все остальные ничего не значат: суперпользователю политика
    не писана, и тест был бы зелёным на полностью открытой базе.
    """
    rows = database.owner(
        f"select rolsuper, rolbypassrls from pg_roles where rolname = '{APP_ROLE}';"
    )
    if not rows:
        return [f"роли {APP_ROLE} в кластере нет: сервису не под кем ходить"]

    superuser, bypass = rows[0]
    problems = []
    if superuser == "t":
        problems.append(f"{APP_ROLE} — суперпользователь: политика на него не действует")
    if bypass == "t":
        problems.append(f"{APP_ROLE} имеет bypassrls: политика на него не действует")
    return problems


def only_own_rows_are_visible(database: Database) -> list[str]:
    """ГЛАВНЫЙ СЛУЧАЙ: `select` без `where` не показывает чужого."""
    problems = []
    for tenant, label in ((TENANT_A, "А"), (TENANT_B, "Б")):
        for table in TABLES:
            rows = database.app(
                f"select count(*), count(*) filter (where tenant_id <> '{tenant}') from {table};",
                tenant,
            )
            visible, foreign = (int(value) for value in rows[0])
            if foreign:
                problems.append(
                    f"{table}: под арендатором {label} видно {foreign} чужих строк "
                    f"при запросе без условия"
                )
            if not visible:
                problems.append(
                    f"{table}: под арендатором {label} не видно ни одной своей строки — "
                    f"политика закрыла и своё"
                )
    return problems


def foreign_row_is_not_found_by_id(database: Database) -> list[str]:
    """Прямое обращение по идентификатору тоже не находит чужого."""
    problems = []
    mine = database.app(f"select count(*) from identity_person where id = '{STUDENT_A}';", TENANT_A)
    if int(mine[0][0]) != 1:
        problems.append("под своим арендатором не находится собственная строка по id")

    foreign = database.app(
        f"select count(*) from identity_person where id = '{STUDENT_A}';", TENANT_B
    )
    if int(foreign[0][0]) != 0:
        problems.append("чужая строка находится по прямому обращению по id")
    return problems


def joins_do_not_leak(database: Database) -> list[str]:
    """Джойн и декартово произведение не проносят чужие строки."""
    rows = database.app("""
select (select count(*) from identity_person p
          join identity_guardianship g
            on g.tenant_id = p.tenant_id and g.student_id = p.id),
       (select count(*) from identity_person, identity_tenant);
""", TENANT_A)
    joined, product = (int(value) for value in rows[0])

    problems = []
    if joined != 1:
        problems.append(f"джойн под арендатором А вернул {joined} строк вместо 1")
    if product != 2:
        problems.append(
            f"произведение person × tenant под арендатором А вернуло {product} строк вместо 2"
        )
    return problems


def nothing_is_visible_without_the_parameter(database: Database) -> list[str]:
    """Забытый параметр сессии — пусто, а не «всё подряд»."""
    problems = []
    for label, tenant in (("параметр не объявлен", None), ("параметр пустой", "")):
        for table in TABLES:
            rows = database.app(f"select count(*) from {table};", tenant)
            if int(rows[0][0]) != 0:
                problems.append(f"{table}: {label}, а строки видны")
    return problems


def the_declaration_does_not_outlive_the_transaction(database: Database) -> list[str]:
    """ОБЯЗАТЕЛЬНЫЙ СЛУЧАЙ: соединение не уносит арендатора следующему.

    Главная утечка приложения выглядит не как дыра в политике, а как забытое
    объявление: соединение вернулось в пул с чужим `pdr.tenant_id`, и запрос
    СЛЕДУЮЩЕГО человека прошёл под предыдущим арендатором. Ни один запрос при
    этом не «сломан» — каждый честно спросил своё, а ответ пришёл чужой.

    Решает здесь не наш код, а третий аргумент `set_config`: `true` привязывает
    объявление к транзакции, `false` — к соединению. По исходнику это уже
    сверено (scripts/check_rls.py), но что `true` значит именно то, о чём мы
    думаем, показывает только настоящая база.

    Всё идёт в ОДНОЙ сессии psql: между транзакциями соединение то же самое —
    ровно как в пуле. Второй арендатор берёт его сразу после первого.
    """
    rows = database.app(f"""
begin;
select set_config('{PARAMETER}', '{TENANT_A}', true);
select 'первый', count(*) filter (where tenant_id = '{TENANT_A}'), count(*) from identity_person;
commit;
select 'между', coalesce(nullif(current_setting('{PARAMETER}', true), ''), 'пусто'),
       (select count(*) from identity_person);
begin;
select set_config('{PARAMETER}', '{TENANT_B}', true);
select 'второй', count(*) filter (where tenant_id = '{TENANT_B}'), count(*) from identity_person;
rollback;
select 'после отката', coalesce(nullif(current_setting('{PARAMETER}', true), ''), 'пусто'),
       (select count(*) from identity_person);
""")
    said = {row[0]: row[1:] for row in rows if len(row) >= 3}
    missing = [label for label in ("первый", "между", "второй", "после отката")
               if label not in said]
    if missing:
        return [f"база не ответила на шаги {', '.join(missing)}: проверять нечего"]

    problems = []
    for label, tenant in (("первый", "А"), ("второй", "Б")):
        own, total = (int(value) for value in said[label])
        if total != own:
            problems.append(
                f"{label} арендатор ({tenant}) видит {total - own} чужих строк: "
                f"объявление не работает вовсе"
            )
        if own != 2:
            problems.append(
                f"{label} арендатор ({tenant}) видит {own} своих строк вместо 2: "
                f"засев не тот, случай ничего не доказывает"
            )

    for label, ended in (("между", "фиксации"), ("после отката", "отката")):
        parameter, visible = said[label][0], int(said[label][1])
        if parameter != "пусто":
            problems.append(
                f"после {ended} на соединении остался арендатор «{parameter}»: "
                f"следующий запрос пойдёт от чужого имени"
            )
        if visible:
            problems.append(
                f"после {ended} без объявления видно {visible} строк: соединение "
                f"вернулось в пул с чужими правами"
            )
    return problems


def garbage_in_the_parameter_is_refused(database: Database) -> list[str]:
    """Мусор в параметре — отказ, а не чужие строки."""
    code = database.app_refusal("select count(*) from identity_person;", "не-uuid")
    if code != "22P02":
        return [f"мусор в параметре дал «{code or 'успех'}» вместо отказа 22P02"]
    return []


def foreign_insert_is_refused(database: Database) -> list[str]:
    """Вставку чужой строки отвергает `with check` политики."""
    code = database.app_refusal(f"""
insert into identity_person (tenant_id, id, display_name, tz)
values ('{TENANT_B}', '{NEW_ROW}', 'Подложенный', 'Europe/Moscow');
""", TENANT_A)
    if code != "42501":
        return [f"вставка с чужим арендатором дала «{code or 'успех'}» вместо отказа 42501"]
    return []


def insert_without_tenant_is_refused(database: Database) -> list[str]:
    """Вставка без арендатора не проходит.

    Код может быть двух видов, и оба означают отказ: политика проверяет строку
    раньше ограничения not null, поэтому обычно приходит 42501, а не 23502.
    Порядок этих двух проверок — внутреннее дело Postgres, привязываться к нему
    незачем.
    """
    code = database.app_refusal(f"""
insert into identity_person (id, display_name, tz)
values ('{NEW_ROW}', 'Ничей', 'Europe/Moscow');
""", TENANT_A)
    if code not in {"42501", "23502"}:
        return [f"вставка без арендатора дала «{code or 'успех'}» вместо отказа"]
    return []


def own_insert_passes_and_stays_own(database: Database) -> list[str]:
    """Своя вставка проходит — и видна только своему."""
    problems = []
    database.app(f"""
insert into identity_person (tenant_id, id, display_name, tz)
values ('{TENANT_A}', '{NEW_ROW}', 'Свой', 'Europe/Moscow');
""", TENANT_A)

    mine = database.app(f"select count(*) from identity_person where id = '{NEW_ROW}';", TENANT_A)
    if int(mine[0][0]) != 1:
        problems.append("собственная вставка не видна её же арендатору")

    theirs = database.app(f"select count(*) from identity_person where id = '{NEW_ROW}';", TENANT_B)
    if int(theirs[0][0]) != 0:
        problems.append("свежевставленная строка видна чужому арендатору")

    database.owner(f"delete from identity_person where id = '{NEW_ROW}';")
    return problems


def bare_delete_touches_only_own_rows(database: Database) -> list[str]:
    """`delete` без `where` вычищает своё, а не базу.

    Делается в транзакции с откатом: проверяется охват удаления, а не умение
    удалять.
    """
    rows = database.app("""
begin;
with removed as (delete from identity_guardianship returning 1) select count(*) from removed;
rollback;
""", TENANT_B)
    removed = int(rows[0][0])
    if removed != 1:
        return [f"delete без условия под арендатором Б задел {removed} строк вместо 1"]

    survived = database.owner("select count(*) from identity_guardianship;")
    if int(survived[0][0]) < 2:
        return ["откат не вернул строки: проверять охват удаления больше нечем"]
    return []


def the_journal_is_append_only(database: Database) -> list[str]:
    """Строку журнала доступа не поправить и не стереть из-под приложения.

    Права роли — только `select` и `insert` (V005__access_log.sql). Журнал, из
    которого можно убрать строку, отвечает на вопрос «кто смотрел в марте» не
    сам, а голосом того, у кого была причина его подчистить. Проверяется здесь,
    потому что решают это ГРАНТЫ в живом кластере, а не наша аккуратность.
    """
    problems = []
    attempts = (
        ("правка", f"update identity_access_log set resource_kind = 'chat' "
                   f"where id = '{LOG_A}';"),
        ("удаление", f"delete from identity_access_log where id = '{LOG_A}';"),
    )
    for what, sql in attempts:
        code = database.app_refusal(sql, TENANT_A)
        if code != "42501":
            problems.append(
                f"{what} строки журнала доступа дало «{code or 'успех'}» вместо отказа 42501"
            )

    rows = database.app(f"select count(*) from identity_access_log where id = '{LOG_A}';", TENANT_A)
    if int(rows[0][0]) != 1:
        problems.append("после отказов строка журнала всё же изменилась или исчезла")
    return problems


def protection_cannot_be_switched_off(database: Database) -> list[str]:
    """Роль приложения не может ни снять защиту, ни заглянуть в реестр миграций."""
    problems = []
    code = database.app_refusal("alter table identity_person disable row level security;", TENANT_A)
    if code != "42501":
        problems.append(
            f"роль {APP_ROLE} выключает построчную защиту: «{code or 'успех'}» вместо отказа 42501"
        )

    code = database.app_refusal("select count(*) from schema_version;", TENANT_A)
    if code != "42501":
        problems.append(
            f"роль {APP_ROLE} читает реестр миграций: «{code or 'успех'}» вместо отказа 42501"
        )
    return problems


CASES = (
    ("защита включена, форсирована и с политикой на каждой таблице", protection_is_on),
    ("роль приложения обычная: не суперпользователь, не bypassrls", app_role_is_ordinary),
    ("ГЛАВНЫЙ: select без where не показывает чужих строк", only_own_rows_are_visible),
    ("чужая строка не находится и по прямому id", foreign_row_is_not_found_by_id),
    ("джойн и произведение не проносят чужого", joins_do_not_leak),
    ("без параметра сессии не видно ничего", nothing_is_visible_without_the_parameter),
    ("ОБЯЗАТЕЛЬНЫЙ: объявление арендатора не переживает транзакцию",
     the_declaration_does_not_outlive_the_transaction),
    ("мусор в параметре — отказ", garbage_in_the_parameter_is_refused),
    ("вставка с чужим арендатором отвергается", foreign_insert_is_refused),
    ("вставка без арендатора отвергается", insert_without_tenant_is_refused),
    ("своя вставка проходит и остаётся своей", own_insert_passes_and_stays_own),
    ("delete без where трогает только своё", bare_delete_touches_only_own_rows),
    ("журнал доступа не правится и не исчезает", the_journal_is_append_only),
    ("защиту не выключить из-под приложения", protection_cannot_be_switched_off),
)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Изоляция арендаторов на живой базе.")
    parser.add_argument("--psql", default="psql", help="чем спрашивать базу")
    arguments = parser.parse_args(argv)

    database = Database(arguments.psql)
    failed = 0

    try:
        seed(database)
        for name, case in CASES:
            problems = case(database)
            if problems:
                failed += 1
                print(f"  ПРОВАЛ  {name}", file=sys.stderr)
                for line in problems:
                    print(f"          {line}", file=sys.stderr)
            else:
                print(f"  ок      {name}")
    except DatabaseError as error:
        print(str(error), file=sys.stderr)
        return 2
    finally:
        try:
            teardown(database)
        except DatabaseError as error:
            print(f"убрать за собой не удалось: {error}", file=sys.stderr)

    if failed:
        print(f"\nПровалено случаев: {failed} из {len(CASES)}. Утечка между арендаторами — "
              f"это чужой ребёнок в чужом кабинете, а не «неудобство».", file=sys.stderr)
        return 1

    print(f"\nИзоляция проверена: {len(CASES)} случаев, чужого не видно ни одним запросом.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
