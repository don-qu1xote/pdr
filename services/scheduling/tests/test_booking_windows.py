"""ОКНА БРОНИРОВАНИЯ СКВОЗЬ ВСЕ СЛОИ: ручка, сценарий, доменное правило, база.

Здесь проверяется не арифметика окон — её проверяют unit-прогон и живой набор, —
а то, что правило действительно стоит на пути записи и что оно работает БЕЗ
ЕДИНОЙ НАСТРОЙКИ: ни одной строки послаблений в базе, ни одной правки
динамического конфига, только умолчания из реестра.

Умолчания эти — два часа на запись и тридцать дней горизонта
(`PDR_BOOKING_WINDOWS`, `PDR_SCHEDULE_HORIZON`), и оба края проверяются здесь
живым процессом, а не подменённым значением.
"""

import uuid

from contour import CABINET
from contour import GUARDIAN
from contour import MOSCOW
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import in_hours
from contour import sign_in

HOUR = 3600000000
DAY = 24 * HOUR


def key():
    return f'windows-{uuid.uuid4()}'


async def book(service_client, headers, starts_at):
    return await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': starts_at,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**headers, 'Idempotency-Key': key()},
    )


async def incoming(service_client, cabinet):
    """Кто просит СНАРУЖИ. Опекун с открытым уровнем — настоящий входящий: он
    записывает подопечного и своими эти часы не считает.

    Сам ученик здесь не годится, и это не про окна: без даты рождения права ему
    не открыты вовсе (`too_young`), и отказ пришёл бы раньше, чем правило успело
    бы сработать. Возраст — вопрос identity, и подменять им проверку окон значило
    бы проверять не то.
    """
    cabinet.open_schedule_to_the_guardian()
    return await sign_in(service_client, GUARDIAN)


def relax(rows, book_before_minutes):
    """Послабление этой паре — строкой, как его положит будущая ручка."""
    rows.execute(
        'insert into scheduling_booking_window '
        '(tenant_id, tutor_id, student_id, book_before_minutes, granted_by) '
        'values (%s, %s, %s, %s, %s)',
        (CABINET, TUTOR, STUDENT, book_before_minutes, TUTOR),
    )
    rows.connection.commit()


async def test_an_incoming_request_books_inside_the_default_windows(service_client, cabinet):
    """БЕЗ ЕДИНОЙ НАСТРОЙКИ ЗАПИСЬ РАБОТАЕТ. Занятие через неделю — обычный
    случай, и умолчания ему не мешают."""
    asking = await incoming(service_client, cabinet)

    booked = await book(service_client, asking, in_a_week())

    assert booked.status == 200, booked.text


async def test_an_incoming_request_cannot_book_at_the_last_moment(service_client, cabinet):
    """ГЛАВНОЕ УТВЕРЖДЕНИЕ ЗАДАЧИ: чужое расписание не занимают за минуту до
    начала. Умолчание — два часа, и час до начала в него не влезает."""
    asking = await incoming(service_client, cabinet)

    refused = await book(service_client, asking, in_hours(1))

    assert refused.status == 409, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:booking_too_late'


async def test_an_incoming_request_cannot_book_beyond_the_horizon(service_client, cabinet):
    """Второй край того же окна: занять весь следующий год нельзя."""
    asking = await incoming(service_client, cabinet)

    refused = await book(service_client, asking, in_a_week() + 60 * DAY)

    assert refused.status == 409, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:booking_too_far'


async def test_the_tutor_is_not_bound_by_his_own_windows(service_client, cabinet):
    """ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: репетитор обходит собственные окна.

    Тот же слот, тот же час, та же практика — отличается только тот, кто нажал.
    Договорился голосом и ставит занятие через час: это его расписание.
    """
    tutor = await sign_in(service_client, TUTOR)

    booked = await book(service_client, tutor, in_hours(1))

    assert booked.status == 200, booked.text

    tutor_far = await book(service_client, tutor, in_a_week() + 60 * DAY)
    assert tutor_far.status == 200, tutor_far.text


async def test_a_relaxation_lets_this_pair_book_later(service_client, cabinet, pgsql):
    """ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: послабление доезжает до записи.

    Тот же запрос, что отклонялся выше, проходит — потому что у этой пары
    появилась строка. Ни конфиг, ни код при этом не менялись.
    """
    asking = await incoming(service_client, cabinet)
    assert (await book(service_client, asking, in_hours(1))).status == 409

    relax(pgsql['pdr'].cursor(), book_before_minutes=15)

    booked = await book(service_client, asking, in_hours(1))

    assert booked.status == 200, booked.text
