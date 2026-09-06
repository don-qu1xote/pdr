"""ИСХОДЯЩАЯ ОЧЕРЕДЬ СКВОЗЬ ВЕСЬ ПРОЦЕСС: обращение человека, транзакция, разбор.

Здесь проверяется ровно тот шов, ради которого задача и написана: строка очереди
ложится В ТОЙ ЖЕ транзакции, что и занятие. Проверяется он с двух сторон, и обе
нужны:

* занятие записалось — строки лежат;
* занятие НЕ записалось — строк нет ни одной. Без этого «в той же транзакции»
  не отличается от «сразу после», а разница между ними — это письмо о занятии,
  которого не случилось.

Разбор очереди под контуром идёт ПО ТРЕБОВАНИЮ, а не по часам: отправщик,
крутящийся сам по себе, разберёт очередь посреди проверки, и «падение между
записью и отправкой» станет зависеть от того, кто успел первым. Сна в наборе нет
и быть не может — его ловит scripts/check_testsuite.py.

Что двое не отправят одно письмо дважды и что сдавшаяся строка ложится в базу с
причиной, проверено там, где это правда, — на живой базе двумя одновременными
захватами (libs/pdr-notifications/tests/postgres_outbox_live_test.cpp).
"""

import uuid

from contour import MOSCOW
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import sign_in

HOUR = 3600000000
DAY = 24 * HOUR

MARCH = in_a_week()

DISPATCHER = 'notifications-outbox-dispatcher'

BOOKED = 'scheduling.lesson_booked'
DAY_BEFORE = 'notifications.reminder_day_before'
HOUR_BEFORE = 'notifications.reminder_hour_before'


def key():
    return f'outbox-{uuid.uuid4()}'


async def book(service_client, headers, starts_at=MARCH, used=None):
    return await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': starts_at,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**headers, 'Idempotency-Key': used or key()},
    )


class Queue:
    """Очередь глазами набора: только то, что о ней спрашивают."""

    def __init__(self, rows):
        self.rows = rows

    def rows_of(self, event_type=None):
        if event_type is None:
            self.rows.execute(
                'select event_type, state, dedup_key from notifications_outbox '
                'order by event_type, dedup_key',
            )
        else:
            self.rows.execute(
                'select event_type, state, dedup_key from notifications_outbox '
                'where event_type = %s order by dedup_key',
                (event_type,),
            )
        return self.rows.fetchall()

    def states(self):
        self.rows.execute('select state, count(*) from notifications_outbox group by state')
        return dict(self.rows.fetchall())

    def payload_of(self, event_type):
        self.rows.execute(
            'select payload from notifications_outbox where event_type = %s limit 1',
            (event_type,),
        )
        return self.rows.fetchone()[0]

    def due_at_of(self, event_type):
        self.rows.execute(
            'select min(next_attempt_at) from notifications_outbox where event_type = %s',
            (event_type,),
        )
        return self.rows.fetchone()[0]


def queue(pgsql):
    return Queue(pgsql['pdr'].cursor())


async def test_booking_leaves_the_letters_and_the_reminders(service_client, cabinet, pgsql):
    """ЗАНЯТИЕ ЗАПИСАНО — ПИСЬМА УЖЕ ЛЕЖАТ, и лежат вместе с ним.

    Ручка ответила успехом, а значит транзакция закоммичена; строки очереди
    видны в той же базе сразу, без всякого отправщика.
    """
    tutor = await sign_in(service_client, TUTOR)

    created = await book(service_client, tutor)
    assert created.status == 200, created.text

    lying = queue(pgsql)
    assert len(lying.rows_of(BOOKED)) == 2, 'о записи сказали не обеим сторонам'
    assert len(lying.rows_of(DAY_BEFORE)) == 2, 'напоминание за сутки не заведено'
    assert len(lying.rows_of(HOUR_BEFORE)) == 2, 'напоминание за час не заведено'
    assert lying.states() == {'pending': 6}, 'кто-то успел отправить письма до отправщика'


async def test_a_refused_booking_leaves_no_letters(service_client, cabinet, pgsql):
    """ГЛАВНОЕ УТВЕРЖДЕНИЕ ЗАДАЧИ, со стороны отката.

    Второе занятие в тот же слот отвергается доменом, и транзакция откатывается
    целиком. Если бы очередь писалась своей транзакцией, письмо о несостоявшемся
    занятии осталось бы лежать — и ушло бы человеку.
    """
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    before = queue(pgsql).rows_of()

    refused = await book(service_client, tutor)

    assert refused.status == 409, refused.text
    assert queue(pgsql).rows_of() == before, 'отказ оставил после себя письма'


async def test_nothing_is_lost_when_the_dispatcher_never_ran(service_client, cabinet, pgsql):
    """ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: падение между записью и отправкой ничего
    не теряет.

    Падение здесь настоящее, а не изображённое: отправщик под контуром сам не
    просыпается вовсе, то есть первого прохода НЕ БЫЛО. Строки всё это время
    лежат, и следующий проход их отправляет.
    """
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    lying = queue(pgsql)
    assert lying.states() == {'pending': 6}, 'что-то ушло без отправщика'

    await service_client.run_task(DISPATCHER)

    assert lying.states()['sent'] == 2, 'письма о записи не ушли после перезапуска'
    assert lying.states()['pending'] == 4, 'напоминания ушли раньше своего часа'


async def test_a_reminder_waits_for_its_hour(service_client, cabinet, pgsql):
    """ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: напоминание срабатывает в назначенное
    время.

    Никакого крона по занятиям для этого нет: строка лежит со сроком в будущем и
    ждёт. Проверяется это самим сроком — он ровно на сутки и на час раньше
    начала, — и тем, что проход отправщика её не берёт.
    """
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    lying = queue(pgsql)
    starts_at = MARCH / 1_000_000

    assert lying.due_at_of(DAY_BEFORE).timestamp() == starts_at - 24 * 3600
    assert lying.due_at_of(HOUR_BEFORE).timestamp() == starts_at - 3600

    await service_client.run_task(DISPATCHER)
    await service_client.run_task(DISPATCHER)

    assert len(lying.rows_of(DAY_BEFORE)) == 2
    assert all(state == 'pending' for _, state, _ in lying.rows_of(DAY_BEFORE)), (
        'напоминание ушло раньше времени'
    )


async def test_the_letter_carries_its_dedup_key(service_client, cabinet, pgsql):
    """КЛЮЧ НАМЕРЕНИЯ ЕДЕТ В НАГРУЗКЕ, а не только стоит колонкой.

    Доставка обещана «не менее одного раза»: повтор узнаёт получатель, а колонки
    чужой таблицы ему читать нечем.
    """
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    payload = queue(pgsql).payload_of(BOOKED)

    assert payload['dedup_key'], 'в нагрузке нет ключа намерения'
    assert payload['channel'] in {'push', 'email'}
    assert 'recipient' in payload


async def test_a_repeated_request_does_not_write_a_second_letter(
    service_client, cabinet, pgsql,
):
    """Повтор по тому же ключу повтора не превращается во второе письмо."""
    tutor = await sign_in(service_client, TUTOR)

    used = key()
    assert (await book(service_client, tutor, used=used)).status == 200
    before = queue(pgsql).rows_of()

    again = await book(service_client, tutor, used=used)

    assert again.headers['Idempotency-Replayed'] == 'true'
    assert queue(pgsql).rows_of() == before, 'повтор запроса написал человеку второй раз'
