#!/usr/bin/env python3
"""Идемпотентность мутирующих запросов на ЖИВОЙ базе.

Заведено до биллинга намеренно: если вводить защиту от повтора вместе с
деньгами, первый же баг стоит двойного списания у живого человека. Проверять её
на фейке недостаточно — весь смысл в том, что делает БАЗА, когда два обращения с
одним ключом приходят одновременно на разные реплики. Фейк про вторую реплику не
знает вовсе.

Проверяется пять вещей, и каждая — отдельный способ получить двойную операцию:

* повтор с тем же ключом и ТЕМ ЖЕ телом не занимает ключ второй раз: вставка
  упирается в первичный ключ, а не в проверку «а нет ли уже такого»;
* повтор с тем же ключом и ДРУГИМ телом виден как другое тело: отпечатки
  различаются, и это ошибка клиента, а не повтор;
* ОДНОВРЕМЕННЫЙ повтор, пока первая транзакция идёт: второй ЖДЁТ, а не
  выполняет операцию параллельно. Это главный случай задачи;
* падение между операцией и записью ключа не оставляет несогласованного
  состояния: и строка ключа, и след операции откатываются вместе;
* ключ принадлежит арендатору: «idempotency-1» у двух практик — два разных
  ключа, и чужой не виден.

Тест пишет в базу профиля и убирает за собой: живого продакшена у проекта нет
(ADR-0007), а на чужих данных такое не запускают.

Запросы здесь — те же, что выполняет `PostgresIdempotencyKeys`. Они скопированы
намеренно: проверять надо тот механизм, который работает в сервисе, а не похожий
на него.

Подключение — обычными переменными PG*, их выставляет Makefile из профиля:

    make test-idempotency
    make test-idempotency ENV_PROFILE=ci
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_isolation as live  # noqa: E402  (после правки sys.path)
import check_jobs as jobs  # noqa: E402  (после правки sys.path)

TENANT_A = "0e0e0e0e-0000-4000-8000-000000000001"
TENANT_B = "0e0e0e0e-0000-4000-8000-000000000002"

TUTOR_A = "0e0e0e0e-0000-4000-8000-00000000a001"
TUTOR_B = "0e0e0e0e-0000-4000-8000-00000000b001"

PERSON_LEFT = "0e0e0e0e-0000-4000-8000-0000000000a1"
PERSON_RIGHT = "0e0e0e0e-0000-4000-8000-0000000000a2"

BODY = "a" * 64
OTHER_BODY = "b" * 64

ANSWER = '{"id": 7}'

TABLE = "http_idempotency_key"

TAKE = """
with taken as (
  insert into http_idempotency_key
    (tenant_id, key, request_fingerprint, state, expires_at)
  values ('{tenant}'::uuid, '{key}', '{fingerprint}', 'in_progress',
          now() + interval '24 hours')
  on conflict (tenant_id, key) do nothing
  returning request_fingerprint, state, response_status, response_body, true as mine
)
select request_fingerprint, state,
       coalesce(response_status::text, '') as answer_status,
       coalesce(response_body, '') as answer_body, mine from taken
union all
select request_fingerprint, state,
       coalesce(response_status::text, '') as answer_status,
       coalesce(response_body, '') as answer_body, false as mine
  from http_idempotency_key
 where key = '{key}' and not exists (select 1 from taken);
"""

COMPLETE = """
update http_idempotency_key
   set state = 'completed', response_status = {status}, response_body = '{body}'
 where key = '{key}' and tenant_id = '{tenant}'::uuid and state = 'in_progress';
"""


def seed(database: live.Database) -> None:
    clean(database)
    database.owner(f"""
insert into identity_tenant (tenant_id, name, tz) values
    ('{TENANT_A}', 'Практика А', 'Europe/Moscow'),
    ('{TENANT_B}', 'Практика Б', 'Asia/Tbilisi');
insert into identity_person (tenant_id, id, display_name, email, tz) values
    ('{TENANT_A}', '{TUTOR_A}', 'Репетитор А', 'a-idem@example.test', 'Europe/Moscow'),
    ('{TENANT_B}', '{TUTOR_B}', 'Репетитор Б', 'b-idem@example.test', 'Asia/Tbilisi');
""")


def clean(database: live.Database) -> None:
    tenants = f"('{TENANT_A}', '{TENANT_B}')"
    database.owner(f"""
delete from {TABLE} where tenant_id in {tenants};
delete from identity_person where tenant_id in {tenants};
delete from identity_tenant where tenant_id in {tenants};
""")


def take(database: live.Database, tenant: str, key: str, fingerprint: str) -> list[list[str]]:
    return database.app(TAKE.format(tenant=tenant, key=key, fingerprint=fingerprint), tenant)


def a_repeat_does_not_take_the_key_twice(database: live.Database) -> list[str]:
    """Повтор с тем же телом ключ второй раз не занимает и операцию не запускает."""
    key = "repeat-same-body"
    problems = []

    first = take(database, TENANT_A, key, BODY)
    if not first or first[0][4] != "t":
        problems.append("первое обращение не заняло ключ")

    database.app(COMPLETE.format(status=200, body='{"id": 1}', key=key, tenant=TENANT_A),
                 TENANT_A)

    second = take(database, TENANT_A, key, BODY)
    if not second:
        problems.append("повтор не увидел занятого ключа вовсе")
    elif second[0][4] != "f":
        problems.append("повтор занял ключ второй раз: операция выполнится дважды")
    elif second[0][1] != "completed":
        problems.append(f"повтор увидел состояние «{second[0][1]}» вместо completed")
    elif second[0][3] != '{"id": 1}':
        problems.append(f"повтор получил тело «{second[0][3]}» вместо сохранённого")

    rows = database.app(f"select count(*) from {TABLE} where key = '{key}';", TENANT_A)
    if int(rows[0][0]) != 1:
        problems.append(f"строк на ключ {key}: {rows[0][0]} вместо одной")
    return problems


def a_changed_body_is_seen_as_a_different_request(database: live.Database) -> list[str]:
    """Тот же ключ с другим телом — ошибка клиента, а не повтор."""
    key = "repeat-other-body"
    problems = []

    take(database, TENANT_A, key, BODY)
    database.app(COMPLETE.format(status=200, body="{}", key=key, tenant=TENANT_A), TENANT_A)

    again = take(database, TENANT_A, key, OTHER_BODY)
    if not again:
        problems.append("обращение с другим телом не увидело занятого ключа")
    elif again[0][4] == "t":
        problems.append("другое тело заняло тот же ключ: два разных запроса под одним ключом")
    elif again[0][0] == OTHER_BODY:
        problems.append("отпечаток переписан чужим телом: подмена запроса прошла незамеченной")
    return problems


def a_concurrent_repeat_makes_exactly_one_thing(database: live.Database, psql: str) -> list[str]:
    """ГЛАВНЫЙ СЛУЧАЙ: два одинаковых запроса ОДНОВРЕМЕННО, в разных сессиях.

    Здесь важна именно одновременность: «сначала посмотрим, потом вставим» даёт
    обоим «ключ свободен» ровно в этот момент, и операция выполняется дважды.

    Обе сессии делают ВИДИМОЕ действие — заводят человека, — и проверяется не
    только строка ключа, но и он: «ключ занят один раз» и «сущность заведена
    одна» это разные утверждения, и второе и есть то, ради чего всё написано.
    Мьютекс в процессе тут не помог бы вовсе: сессии разные, как разными бывают
    реплики.
    """
    key = "at-once-together"
    problems = []

    def racing(person: str) -> subprocess.Popen[str]:
        return jobs.psql_async(psql, f"""
set role {live.APP_ROLE};
select set_config('{live.PARAMETER}', '{TENANT_A}', false);
create temporary table claimed on commit drop as
{TAKE.format(tenant=TENANT_A, key=key, fingerprint=BODY).strip().rstrip(';')};
insert into identity_person (tenant_id, id, display_name, email, tz)
select '{TENANT_A}', '{person}', 'Заведён одновременно', null, 'Europe/Moscow'
 where exists (select 1 from claimed where mine);
select pg_sleep(0.7);
update http_idempotency_key
   set state = 'completed', response_status = 200, response_body = '{ANSWER}'
 where key = '{key}' and tenant_id = '{TENANT_A}'::uuid and state = 'in_progress'
   and exists (select 1 from claimed where mine);
""")

    left = racing(PERSON_LEFT)
    right = racing(PERSON_RIGHT)
    for session, side in ((left, "левая"), (right, "правая")):
        _, complaint = session.communicate()
        if session.returncode != 0:
            problems.append(f"{side} сессия завершилась ошибкой: {complaint.strip()}")

    made = database.owner(
        f"select count(*) from identity_person where id in ('{PERSON_LEFT}', '{PERSON_RIGHT}');"
    )
    if int(made[0][0]) != 1:
        problems.append(
            f"одновременный повтор завёл сущностей: {made[0][0]} вместо одной. Это и есть "
            f"двойное списание, только без денег"
        )

    rows = database.owner(f"select count(*) from {TABLE} where key = '{key}';")
    if int(rows[0][0]) != 1:
        problems.append(f"строк на ключ {key}: {rows[0][0]} вместо одной")

    after = take(database, TENANT_A, key, BODY)
    if not after:
        problems.append("после обеих сессий ключ не нашёлся вовсе")
    elif after[0][4] == "t":
        problems.append("после обеих сессий ключ удалось занять третий раз")
    elif after[0][3] != ANSWER:
        problems.append(f"пришедший следом получил «{after[0][3]}» вместо сохранённого ответа")
    return problems


def a_failure_leaves_nothing_behind(database: live.Database, psql: str) -> list[str]:
    """Падение между операцией и записью ключа не оставляет полработы.

    Ключ и операция — одна транзакция, поэтому откатывается либо всё, либо
    ничего. Проверяется именно это: сессия занимает ключ, делает видимое
    действие и умирает, — и после неё не остаётся ни ключа, ни действия.
    """
    key = "died-halfway"
    problems = []

    died = jobs.psql_async(psql, f"""
set role {live.APP_ROLE};
select set_config('{live.PARAMETER}', '{TENANT_A}', false);
{TAKE.format(tenant=TENANT_A, key=key, fingerprint=BODY)}
insert into identity_person (tenant_id, id, display_name, email, tz)
values ('{TENANT_A}', '0e0e0e0e-0000-4000-8000-00000000dead', 'Ученик из полработы',
        'halfway@example.test', 'Europe/Moscow');
select 1 / 0;
""")
    died.communicate()
    if died.returncode == 0:
        problems.append("сессия, обязанная упасть, завершилась успехом — проверка ничего не даёт")

    keys = database.owner(f"select count(*) from {TABLE} where key = '{key}';")
    if int(keys[0][0]) != 0:
        problems.append(
            f"после падения осталась строка ключа: {keys[0][0]}. Повтор упрётся в неё и "
            f"операцию не выполнит НИКОГДА"
        )

    people = database.owner(
        f"select count(*) from identity_person "
        f"where id = '0e0e0e0e-0000-4000-8000-00000000dead';"
    )
    if int(people[0][0]) != 0:
        problems.append(
            f"после падения осталась сущность без ключа: {people[0][0]}. Повтор выполнит "
            f"операцию ВТОРОЙ раз"
        )
    return problems


def a_key_belongs_to_its_tenant(database: live.Database) -> list[str]:
    """Ключи придумывает клиент: одинаковые у двух практик — разные ключи."""
    key = "idempotency-1"
    problems = []

    mine = take(database, TENANT_A, key, BODY)
    theirs = take(database, TENANT_B, key, BODY)
    if not mine or mine[0][4] != "t":
        problems.append("ключ практики А не занялся")
    if not theirs or theirs[0][4] != "t":
        problems.append(
            "ключ с тем же именем у другой практики не занялся: клиентские ключи столкнулись, "
            "и посторонний человек получил чужой отказ"
        )

    visible = database.app(f"select count(*) from {TABLE} where key = '{key}';", TENANT_A)
    if int(visible[0][0]) != 1:
        problems.append(f"из практики А видно строк: {visible[0][0]} вместо одной")
    return problems


def the_application_cannot_delete_a_key(database: live.Database) -> list[str]:
    """Убирает ключи уборка под своей ролью, а не приложение.

    Приложение, умеющее удалить ключ, умеет и обойти собственную защиту от
    повтора — одним лишним запросом.
    """
    key = "not-yours-to-delete"
    problems = []

    take(database, TENANT_A, key, BODY)
    code = database.app_refusal(f"delete from {TABLE} where key = '{key}';", TENANT_A)
    if code != "42501":
        problems.append(f"ключ удаляется из-под приложения: «{code or 'успех'}» вместо 42501")
    return problems


def expired_keys_are_swept(database: live.Database) -> list[str]:
    """Уборка забирает просроченные и не трогает живые."""
    problems = []

    database.owner(f"""
insert into {TABLE}
    (tenant_id, key, request_fingerprint, state, created_at, expires_at) values
    ('{TENANT_A}', 'long-expired', '{BODY}', 'in_progress',
     now() - interval '3 days', now() - interval '2 days'),
    ('{TENANT_A}', 'still-alive',  '{BODY}', 'in_progress',
     now(), now() + interval '2 days');
""")

    swept = Path(__file__).resolve().parent.parent / "db" / "http" / "prune.sql"
    if not swept.is_file():
        return [f"{swept}: запроса уборки нет — просроченные ключи копятся вечно"]

    database.owner(swept.read_text(encoding="utf-8"))

    left = database.owner(
        f"select key from {TABLE} where tenant_id = '{TENANT_A}' "
        f"and key in ('long-expired', 'still-alive') order by key;"
    )
    names = [row[0] for row in left]
    if names != ["still-alive"]:
        problems.append(f"после уборки осталось {names} вместо ['still-alive']")
    return problems


CHECKS = (
    ("повтор с тем же телом ключ второй раз не занимает", a_repeat_does_not_take_the_key_twice),
    ("другое тело с тем же ключом видно как другой запрос",
     a_changed_body_is_seen_as_a_different_request),
    ("ключ принадлежит арендатору", a_key_belongs_to_its_tenant),
    ("ключ не удаляется из-под приложения", the_application_cannot_delete_a_key),
    ("просроченные ключи убираются, живые остаются", expired_keys_are_swept),
)

CHECKS_WITH_PSQL = (
    ("ГЛАВНЫЙ: два одинаковых запроса одновременно заводят РОВНО ОДНУ сущность",
     a_concurrent_repeat_makes_exactly_one_thing),
    ("падение между операцией и ключом не оставляет полработы", a_failure_leaves_nothing_behind),
)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Идемпотентность на живой базе.")
    parser.add_argument("--psql", default="psql", help="путь до psql")
    arguments = parser.parse_args(argv)

    database = live.Database(arguments.psql)
    try:
        seed(database)
    except live.DatabaseError as broken:
        print(f"база недоступна: {broken}", file=sys.stderr)
        return 2

    failed = 0
    try:
        for title, check in CHECKS:
            problems = check(database)
            failed += bool(problems)
            print(f"  {'ПРОВАЛ' if problems else 'ок    '}  {title}")
            for line in problems:
                print(f"          {line}")

        for title, check in CHECKS_WITH_PSQL:
            problems = check(database, arguments.psql)
            failed += bool(problems)
            print(f"  {'ПРОВАЛ' if problems else 'ок    '}  {title}")
            for line in problems:
                print(f"          {line}")
    finally:
        clean(database)

    total = len(CHECKS) + len(CHECKS_WITH_PSQL)
    if failed:
        print(f"\nПровалено случаев: {failed} из {total}. Повтор, выполнивший операцию дважды, "
              f"— это второе списание у живого человека.", file=sys.stderr)
        return 1

    print(f"\nИдемпотентность проверена: {total} случаев, повтор операции не производит.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
