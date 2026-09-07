"""ПОДПИСКА НА КАЛЕНДАРЬ СКВОЗЬ ВСЕ СЛОИ: маршрут, форма, опознание, лента.

Здесь проверяется то, чего не видно ни на фейках, ни на живой базе по
отдельности: что ленту отдаёт настоящий процесс, что удостоверением ей служит
секрет В АДРЕСЕ, и что перевыпуск делает старую ссылку нерабочей на самом деле,
а не только в хранилище.

Сверку самой ленты со стандартом делает отдельный набор чужим разборщиком
(tests/contract/icalendar_conformance): «похоже на ICS» и «читается календарём»
— разные утверждения, и второе проверяется только настоящим разборщиком.
"""

import uuid

from contour import CABINET
from contour import MOSCOW
from contour import OUTSIDER
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import sign_in

HOUR = 3600000000

MARCH = in_a_week()


def key():
    return f'calendar-{uuid.uuid4()}'


async def issue(service_client, headers):
    return await service_client.post(
        '/api/v1/calendar-subscription',
        json={},
        headers={**headers, 'Idempotency-Key': key()},
    )


async def test_the_tutor_gets_a_link_and_the_calendar_reads_it(service_client, cabinet):
    """ГЛАВНЫЙ ОБМЕН: выдали ссылку, сходили по ней и получили ленту.

    Ходим по ней БЕЗ cookie и без заголовков — ровно так, как это делает чужой
    календарь: он умеет сходить по ссылке и больше ничего.
    """
    tutor = await sign_in(service_client, TUTOR)

    given = await issue(service_client, tutor)
    assert given.status == 200, given.text
    path = given.json()['path']
    assert path.startswith(f'/api/v1/cabinet/{CABINET}/calendar/')
    assert path.endswith('/schedule.ics')

    feed = await service_client.get(path)

    assert feed.status == 200, feed.text
    assert feed.headers['Content-Type'].startswith('text/calendar')
    assert feed.text.startswith('BEGIN:VCALENDAR')
    assert feed.text.rstrip().endswith('END:VCALENDAR')


async def test_the_booked_lesson_shows_up_in_the_feed(service_client, cabinet):
    """Занятие, назначенное у нас, появляется в ленте — ради этого всё и есть."""
    tutor = await sign_in(service_client, TUTOR)
    booked = await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': MARCH,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    assert booked.status == 200, booked.text

    given = await issue(service_client, tutor)
    feed = await service_client.get(given.json()['path'])

    assert feed.status == 200, feed.text
    assert 'BEGIN:VEVENT' in feed.text
    assert f'UID:lesson-{booked.json()["id"]}@pdr' in feed.text
    assert f'TZID:{MOSCOW}' in feed.text, 'зона названа, а VTIMEZONE не описан'


async def test_the_feed_asks_to_be_cached(service_client, cabinet):
    """Календари ходят сюда часто и без спроса: ответ говорит, до каких пор он годен."""
    tutor = await sign_in(service_client, TUTOR)
    given = await issue(service_client, tutor)

    feed = await service_client.get(given.json()['path'])

    assert feed.status == 200, feed.text
    assert feed.headers['ETag']
    assert 'max-age=' in feed.headers['Cache-Control']
    assert 'REFRESH-INTERVAL' in feed.text

    again = await service_client.get(given.json()['path'])
    assert again.headers['ETag'] == feed.headers['ETag'], (
        'отпечаток поменялся сам по себе — условный запрос не сработает никогда'
    )


async def test_reissuing_makes_the_old_link_dead(service_client, cabinet):
    """ОБЯЗАТЕЛЬНАЯ ПРОВЕРКА ЗАДАЧИ: ссылка утекла — её чинят одним действием."""
    tutor = await sign_in(service_client, TUTOR)
    first = await issue(service_client, tutor)
    assert (await service_client.get(first.json()['path'])).status == 200

    second = await issue(service_client, tutor)
    assert second.json()['path'] != first.json()['path'], 'перевыпуск выдал ту же ссылку'

    dead = await service_client.get(first.json()['path'])
    alive = await service_client.get(second.json()['path'])

    assert dead.status == 401, dead.text
    assert alive.status == 200, alive.text


async def test_a_guessed_link_opens_nothing(service_client, cabinet):
    """Подобранный адрес отдаёт то же, что и чужой: ничего."""
    made_up = 'A' * 43

    guessed = await service_client.get(
        f'/api/v1/cabinet/{CABINET}/calendar/{made_up}/schedule.ics'
    )

    assert guessed.status == 401, guessed.text
    assert guessed.headers['Content-Type'].startswith('application/problem+json')


async def test_a_link_from_another_cabinet_opens_nothing(service_client, cabinet):
    """Тот же секрет в чужом кабинете — чужой секрет: подписка ищется внутри арендатора."""
    tutor = await sign_in(service_client, TUTOR)
    given = await issue(service_client, tutor)
    secret = given.json()['path'].split('/')[-2]

    elsewhere = await service_client.get(
        f'/api/v1/cabinet/{OUTSIDER}/calendar/{secret}/schedule.ics'
    )

    assert elsewhere.status == 401, elsewhere.text


async def test_the_link_belongs_to_the_one_who_asked(service_client, cabinet):
    """Ссылку человек выдаёт СЕБЕ, и открывает она его расписание, а не чужое."""
    student = await sign_in(service_client, STUDENT)
    booked_by_tutor = await sign_in(service_client, TUTOR)
    lesson = await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': MARCH,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**booked_by_tutor, 'Idempotency-Key': key()},
    )
    assert lesson.status == 200, lesson.text

    given = await issue(service_client, student)
    feed = await service_client.get(given.json()['path'])

    assert feed.status == 200, feed.text
    assert f'UID:lesson-{lesson.json()["id"]}@pdr' in feed.text, (
        'ученик не увидел в своей ленте занятие, на которое записан'
    )
