#!/usr/bin/env python3
"""Одиночные задания на ЖИВОЙ базе: то, ради чего заведён DistLock.

Unit-тесты (`ctest -R jobs`) проверяют механизм против фейков: повтор ничего не
делает дважды, потеря блокировки прекращает прогон, возраст последнего запуска
растёт. Здесь спрашивается другое — работают ли обещания в базе:

* блокировку берёт ровно один воркер, второй уходит ни с чем;
* умерший воркер не держит блокировку вечно: она уходит по сроку, а не по
  чьей-то доброй воле. Ровно на этом ломается самодельная блокировка через
  `update ... set locked = true`;
* след действия нельзя поставить дважды даже одновременно: второй ждёт первого
  и получает конфликт, а не второе действие;
* возраст последнего прогона растёт, пока воркер стоит.

Запросы блокировки здесь — те же, что выполняет штатный
`storages::postgres::DistLockStrategy` (userver). Они скопированы намеренно:
проверять надо тот механизм, который работает в сервисе, а не похожий на него.

Тест пишет в базу профиля и убирает за собой: живого продакшена у проекта нет
(ADR-0007), а на чужих данных такое не запускают.

Воркеров здесь двое, и отличаются они идентификатором владельца: у userver он
составной — «хост:локер», и блокировка узнаёт своего держателя именно по нему.

Подключение — обычными переменными PG*, их выставляет Makefile из профиля:

    make test-jobs
    make test-jobs ENV_PROFILE=ci
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_isolation as live  # noqa: E402  (после правки sys.path)

JOB = "pdr-check-jobs"
LOCK = "pdr.check_jobs"

WORKER_A = "host-a:locker-1"
WORKER_B = "host-b:locker-2"

TENANT_A = "0d0d0d0d-0000-4000-8000-000000000001"
TENANT_B = "0d0d0d0d-0000-4000-8000-000000000002"
EXPORT_TENANT = "0e0e0e0e-0000-4000-8000-000000000007"

KEYS = ("lesson-1", "lesson-2", "lesson-3")

ACQUIRE = """
insert into jobs_lock as t (key, owner, expiration_time)
select '{lock}', '{owner}', current_timestamp + make_interval(secs => {ttl})
where not exists (
    select * from jobs_lock
    where key = '{lock}' and owner <> '{owner}' and expiration_time > current_timestamp
)
on conflict (key) do update
set owner = '{owner}', expiration_time = current_timestamp + make_interval(secs => {ttl})
where (t.owner = '{owner}') or (t.expiration_time <= current_timestamp)
returning 1;
"""

RELEASE = """
delete from jobs_lock where key = '{lock}' and owner = '{owner}' returning 1;
"""

CLAIM = """
insert into jobs_effect (tenant_id, job, effect_key)
values (nullif(current_setting('pdr.tenant_id', true), '')::uuid, '{job}', '{key}')
on conflict do nothing
returning 1;
"""

RUN_STARTED = """
insert into jobs_run (job, attempt_at, outcome, runs)
values ('{job}', now(), 'running', 1)
on conflict (job) do update
set attempt_at = excluded.attempt_at, outcome = 'running', runs = jobs_run.runs + 1;
"""

RUN_FINISHED = """
update jobs_run
set started_at = now() - make_interval(secs => {duration} / 1000.0), finished_at = now(),
    duration_ms = {duration}, outcome = '{outcome}',
    produced = {produced}, repeated = {repeated}
where job = '{job}';
"""

SILENT = """
select count(*) from jobs_run
where job = '{job}'
  and (finished_at is null or now() - finished_at > interval '{allowed}');
"""


class Worker:
    """Воркер: берёт блокировку и ставит следы под своим именем."""

    def __init__(self, database: live.Database, owner: str) -> None:
        self._database = database
        self._owner = owner

    def acquire(self, ttl_seconds: float = 10) -> bool:
        """Взял ли блокировку. Пусто в ответе — держит другой."""
        rows = self._database.app(ACQUIRE.format(lock=LOCK, owner=self._owner, ttl=ttl_seconds))
        return bool(rows)

    def release(self) -> bool:
        rows = self._database.app(RELEASE.format(lock=LOCK, owner=self._owner))
        return bool(rows)

    def claim(self, tenant: str, key: str) -> bool:
        """Поставил ли след здесь. Пусто — след уже стоял, действие не повторяется."""
        rows = self._database.app(CLAIM.format(job=JOB, key=key), tenant)
        return bool(rows)


def psql_async(psql: str, sql: str) -> subprocess.Popen[str]:
    """Отдельный ПРОЦЕСС psql: нужен там, где две сессии обязаны пересечься.

    Обёртка над psql из check_isolation.py ждёт ответа, а здесь первая сессия
    должна держать незакрытую транзакцию, пока во вторую приходит соперник.

    Скрипт уезжает одним `-c`, а не в stdin: psql выполняет такую строку в ОДНОЙ
    транзакции, и вставка остаётся незакоммиченной до конца процесса — ровно то,
    что нужно. Заодно у процесса нет открытого канала ввода, и его не приходится
    закрывать раньше, чем понадобится дождаться ответа.
    """
    return subprocess.Popen(
        [psql, "--no-psqlrc", "-v", "ON_ERROR_STOP=1", "-qtA", "-c", sql],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def seed(database: live.Database) -> None:
    teardown(database)
    database.owner(f"""
insert into identity_tenant (tenant_id, name, tz) values
    ('{TENANT_A}', 'Арендатор заданий А', 'Europe/Moscow'),
    ('{TENANT_B}', 'Арендатор заданий Б', 'Asia/Tbilisi'),
    ('{EXPORT_TENANT}', 'Арендатор выгрузки', 'Europe/Moscow');
insert into identity_person (tenant_id, id, display_name, email, tz) values
    ('{EXPORT_TENANT}', '0e0e0e0e-0000-4000-8000-00000000a001',
     'Человек выгрузки', 'export@example.test', 'Europe/Moscow');
insert into identity_role_assignment (tenant_id, id, person_id, role) values
    ('{EXPORT_TENANT}', '0e0e0e0e-0000-4000-8000-00000000d001',
     '0e0e0e0e-0000-4000-8000-00000000a001', 'student');
insert into identity_guardianship (tenant_id, id, guardian_id, student_id) values
    ('{EXPORT_TENANT}', '0e0e0e0e-0000-4000-8000-00000000c001',
     '0e0e0e0e-0000-4000-8000-00000000a001',
     '0e0e0e0e-0000-4000-8000-00000000a001');
insert into observability_product_event
    (tenant_id, id, type, version, actor_role, occurred_at, recorded_at, fields) values
    ('{TENANT_A}', '0d0d0d0d-0000-4000-8000-00000000e001',
     'scheduling.lesson_completed', 1, 'tutor',
     now() - interval '1 hour', now() - interval '1 hour',
     '{{"score": 4}}'),
    ('{EXPORT_TENANT}', '0e0e0e0e-0000-4000-8000-00000000e001',
     'scheduling.lesson_completed', 1, 'tutor',
     now() - interval '1 hour', now() - interval '1 hour',
     '{{"score": 5}}');
""")


def teardown(database: live.Database) -> None:
    """Убрать за собой. Порядок важен: следы ссылаются на арендатора.

    Следы удаляются ПО ЗАДАНИЮ и без условия по арендатору: строка могла
    остаться от прогона, упавшего на середине, и тогда `delete from
    identity_tenant` упёрся бы во внешний ключ.

    EXPORT_TENANT не удаляется: его данные нужны экспорту аккаунта, который
    идёт следом (ci.yml).
    """
    database.owner(f"""
delete from jobs_effect where job = '{JOB}';
delete from jobs_lock where key = '{LOCK}';
delete from jobs_run where job = '{JOB}';
delete from observability_product_event
    where tenant_id in ('{TENANT_A}', '{TENANT_B}');
delete from identity_tenant where tenant_id in ('{TENANT_A}', '{TENANT_B}');
""")


def only_one_worker_holds_the_lock(database: live.Database) -> list[str]:
    """ГЛАВНЫЙ СЛУЧАЙ: двое на одной блокировке — работает один."""
    first = Worker(database, WORKER_A)
    second = Worker(database, WORKER_B)

    problems = []
    if not first.acquire():
        problems.append("первый воркер не смог взять свободную блокировку")
    if second.acquire():
        problems.append("второй воркер взял занятую блокировку: работа пойдёт вдвоём")

    rows = database.owner(f"select owner, count(*) over () from jobs_lock where key = '{LOCK}';")
    if len(rows) != 1:
        problems.append(f"строк блокировки {len(rows)} вместо одной")
    elif rows[0][0] != WORKER_A:
        problems.append(f"блокировкой владеет «{rows[0][0]}», а взял её {WORKER_A}")

    if not first.acquire():
        problems.append("держатель не смог продлить свою же блокировку")
    if second.acquire():
        problems.append("после продления блокировку удалось отобрать")
    return problems


def a_dead_worker_does_not_hold_the_lock_forever(database: live.Database) -> list[str]:
    """Умерший воркер перестаёт продлевать — и блокировка уходит по сроку.

    Это ровно тот случай, на котором ломается самодельная блокировка через
    `update ... set locked = true`: строка остаётся занятой процессом, которого
    больше нет, и снять её некому.
    """
    first = Worker(database, WORKER_A)
    second = Worker(database, WORKER_B)

    problems = []
    if not first.acquire():
        problems.append("первый воркер не смог взять блокировку")

    database.owner(
        f"update jobs_lock set expiration_time = current_timestamp - interval '1 second' "
        f"where key = '{LOCK}';"
    )

    if not second.acquire():
        problems.append("просроченная блокировка не досталась живому воркеру")

    rows = database.owner(f"select owner from jobs_lock where key = '{LOCK}';")
    if not rows or rows[0][0] != WORKER_B:
        problems.append("после перехвата владелец блокировки не сменился")

    if first.release():
        problems.append("воркер отпустил блокировку, которой не владеет")
    if not second.release():
        problems.append("владелец не смог отпустить свою блокировку")
    if database.owner(f"select 1 from jobs_lock where key = '{LOCK}';"):
        problems.append("отпущенная блокировка осталась в таблице")
    return problems


def lock_loss_does_not_send_twice(database: live.Database) -> list[str]:
    """Потеря блокировки посреди работы: второй подхватывает, второй раз никто.

    Первый воркер успевает одну единицу из трёх и теряет блокировку. Второй
    строит тот же план — и упирается в след первого.
    """
    first = Worker(database, WORKER_A)
    second = Worker(database, WORKER_B)
    problems = []

    if not first.acquire():
        problems.append("первый воркер не смог взять блокировку")
    if not first.claim(TENANT_A, KEYS[0]):
        problems.append("первый воркер не смог поставить след своего действия")

    database.owner(
        f"update jobs_lock set expiration_time = current_timestamp - interval '1 second' "
        f"where key = '{LOCK}';"
    )
    if not second.acquire():
        problems.append("второй воркер не смог подхватить работу")

    produced = [key for key in KEYS if second.claim(TENANT_A, key)]
    if produced != list(KEYS[1:]):
        problems.append(f"второй воркер произвёл {produced}, а должен был {list(KEYS[1:])}")

    rows = database.owner(f"""
select effect_key, count(*) from jobs_effect
where job = '{JOB}' and tenant_id = '{TENANT_A}'
group by effect_key order by effect_key;
""")
    if [row[0] for row in rows] != list(KEYS):
        problems.append(f"следов {[row[0] for row in rows]} вместо {list(KEYS)}")
    for key, count in rows:
        if int(count) != 1:
            problems.append(f"действие {key} произведено {count} раза вместо одного")

    second.release()
    return problems


def a_trace_cannot_be_set_twice_at_once(database: live.Database, psql: str) -> list[str]:
    """Два воркера на одном ключе ОДНОВРЕМЕННО: второй ждёт и получает конфликт.

    Здесь важна именно одновременность: «сначала посмотрим, потом вставим» даёт
    обоим «ещё не делали» ровно в этот момент. Первая сессия держит незакрытую
    транзакцию, вторая приходит на тот же ключ.
    """
    key = "at-once"
    problems = []

    holding = psql_async(psql, f"""
set role {live.APP_ROLE};
select set_config('{live.PARAMETER}', '{TENANT_A}', false);
{CLAIM.format(job=JOB, key=key)}
select pg_sleep(1.5);
""")

    try:
        time.sleep(0.5)

        waiting = database.app_refusal(
            f"set lock_timeout = '300ms';\n{CLAIM.format(job=JOB, key=key)}", TENANT_A
        )
        if waiting != "55P03":
            problems.append(
                f"вторая вставка того же ключа дала «{waiting or 'успех'}» вместо ожидания "
                f"55P03: два воркера ставят один след одновременно"
            )
    finally:
        _, complaint = holding.communicate()
        if holding.returncode != 0:
            problems.append(f"сессия, державшая след, завершилась ошибкой: {complaint.strip()}")

    if database.app(CLAIM.format(job=JOB, key=key), TENANT_A):
        problems.append("после коммита первого след удалось поставить второй раз")

    rows = database.owner(
        f"select count(*) from jobs_effect where job = '{JOB}' and effect_key = '{key}';"
    )
    if int(rows[0][0]) != 1:
        problems.append(f"следов на ключ {key}: {rows[0][0]} вместо одного")
    return problems


def a_trace_belongs_to_its_tenant(database: live.Database) -> list[str]:
    """След — чей-то. Чужой не виден и работы чужого арендатора не отменяет."""
    key = "same-key-two-tenants"
    problems = []

    if not Worker(database, WORKER_A).claim(TENANT_A, key):
        problems.append("след арендатора А не поставился")
    if not Worker(database, WORKER_A).claim(TENANT_B, key):
        problems.append("след с тем же ключом у другого арендатора не поставился")

    visible = database.app(
        f"select count(*) from jobs_effect where effect_key = '{key}';", TENANT_A
    )
    if int(visible[0][0]) != 1:
        problems.append(f"под арендатором А видно следов: {visible[0][0]} вместо одного")

    code = database.app_refusal(
        f"update jobs_effect set produced_at = now() where effect_key = '{key}';", TENANT_A
    )
    if code != "42501":
        problems.append(f"след удалось переписать: «{code or 'успех'}» вместо отказа 42501")
    return problems


def the_journal_keeps_when_it_ran_and_how_long(database: live.Database) -> list[str]:
    """Журнал: когда последний раз отработало и сколько заняло."""
    problems = []

    database.app(RUN_STARTED.format(job=JOB))
    database.app(
        RUN_FINISHED.format(job=JOB, duration=1234, outcome="done", produced=3, repeated=1)
    )
    database.app(RUN_STARTED.format(job=JOB))
    database.app(
        RUN_FINISHED.format(job=JOB, duration=7, outcome="lock_lost", produced=0, repeated=3)
    )

    rows = database.owner(f"""
select runs, duration_ms, outcome, produced, repeated,
       finished_at is not null, finished_at >= started_at, attempt_at <= finished_at
from jobs_run where job = '{JOB}';
""")
    if len(rows) != 1:
        problems.append(f"строк журнала {len(rows)} вместо одной: журнал не один на задание")
        return problems

    runs, duration, outcome, produced, repeated, finished, ordered, attempted = rows[0]
    if int(runs) != 2:
        problems.append(f"прогонов в журнале {runs} вместо двух")
    if int(duration) != 7 or outcome != "lock_lost":
        problems.append(f"в журнале остался не последний прогон: {duration} мс, «{outcome}»")
    if int(produced) != 0 or int(repeated) != 3:
        problems.append(f"счётчики последнего прогона: {produced} и {repeated} вместо 0 и 3")
    if finished != "t" or ordered != "t":
        problems.append("прогон не закрыт или закончился раньше, чем начался")
    if attempted != "t":
        problems.append("последняя попытка начата позже конца прогона: перемешаны два прогона")

    database.app(RUN_STARTED.format(job=JOB))
    kept = database.owner(f"""
select finished_at is not null, outcome, attempt_at > finished_at
from jobs_run where job = '{JOB}';
""")
    if kept[0][0] != "t":
        problems.append("новая попытка стёрла конец последнего завершённого прогона")
    if kept[0][1] != "running":
        problems.append(f"идущий прогон помечен как «{kept[0][1]}», а не running")
    if kept[0][2] != "t":
        problems.append("идущая попытка не отличима от прошлого прогона по времени начала")

    code = database.app_refusal(
        f"update jobs_run set outcome = 'кажется, всё хорошо' where job = '{JOB}';"
    )
    if code != "23514":
        problems.append(f"неизвестный исход прошёл в журнал: «{code or 'успех'}» вместо 23514")

    code = database.app_refusal(
        f"update jobs_run set duration_ms = null where job = '{JOB}';"
    )
    if code != "23514":
        problems.append(
            f"прогон без длительности прошёл в журнал: «{code or 'успех'}» вместо 23514"
        )
    return problems


def silence_grows_while_the_worker_is_stopped(database: live.Database) -> list[str]:
    """Метрика возраста последнего запуска растёт при остановленном воркере."""
    problems = []
    database.app(RUN_STARTED.format(job=JOB))
    database.app(RUN_FINISHED.format(job=JOB, duration=5, outcome="done", produced=1, repeated=0))

    def age() -> float:
        rows = database.owner(
            f"select extract(epoch from (now() - finished_at)) from jobs_run where job = '{JOB}';"
        )
        return float(rows[0][0])

    before = age()
    time.sleep(1.1)
    after = age()
    if after <= before:
        problems.append(f"возраст последнего прогона не вырос: {before} → {after}")

    allowed = "1 hour"
    if int(database.owner(SILENT.format(job=JOB, allowed=allowed))[0][0]) != 0:
        problems.append("только что отработавшее задание объявлено замолчавшим")

    database.owner(f"""
update jobs_run
set started_at = now() - interval '25 hours', finished_at = now() - interval '25 hours'
where job = '{JOB}';
""")
    if int(database.owner(SILENT.format(job=JOB, allowed="24 hours"))[0][0]) != 1:
        problems.append("задание, не отрабатывавшее сутки, не видно запросом молчания")

    database.owner(f"""
update jobs_run
set started_at = null, finished_at = null, duration_ms = null, attempt_at = now()
where job = '{JOB}';
""")
    if int(database.owner(SILENT.format(job=JOB, allowed="24 hours"))[0][0]) != 1:
        problems.append("незакрытый прогон не считается молчанием")
    return problems


CASES = (
    ("ГЛАВНЫЙ: двое на одной блокировке — работает один", only_one_worker_holds_the_lock),
    ("умерший воркер не держит блокировку вечно", a_dead_worker_does_not_hold_the_lock_forever),
    ("потеря блокировки не приводит к двойному действию", lock_loss_does_not_send_twice),
    ("один след на ключ даже при одновременной вставке", a_trace_cannot_be_set_twice_at_once),
    ("след принадлежит арендатору и не переписывается", a_trace_belongs_to_its_tenant),
    ("журнал помнит последний прогон и его длительность",
     the_journal_keeps_when_it_ran_and_how_long),
    ("возраст последнего запуска растёт при стоящем воркере",
     silence_grows_while_the_worker_is_stopped),
)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Одиночные задания на живой базе.")
    parser.add_argument("--psql", default="psql", help="чем спрашивать базу")
    arguments = parser.parse_args(argv)

    database = live.Database(arguments.psql)
    failed = 0

    try:
        seed(database)
        for name, case in CASES:
            problems = (
                case(database, arguments.psql)
                if case is a_trace_cannot_be_set_twice_at_once
                else case(database)
            )
            if problems:
                failed += 1
                print(f"  ПРОВАЛ  {name}", file=sys.stderr)
                for line in problems:
                    print(f"          {line}", file=sys.stderr)
            else:
                print(f"  ок      {name}")
    except live.DatabaseError as error:
        print(str(error), file=sys.stderr)
        return 2
    finally:
        try:
            teardown(database)
        except live.DatabaseError as error:
            print(f"убрать за собой не удалось: {error}", file=sys.stderr)

    if failed:
        print(f"\nПровалено случаев: {failed} из {len(CASES)}. Двойная рассылка и второе "
              f"списание — не «неудобство», а деньги и доверие.", file=sys.stderr)
        return 1

    print(f"\nОдиночные задания проверены: {len(CASES)} случаев, двойной работы не "
          f"случается ни при потере блокировки, ни при одновременной вставке.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
