"""ЛЕНТА ЧИТАЕТСЯ НАСТОЯЩИМ РАЗБОРЩИКОМ iCalendar.

Проверяется не то, что мы напечатали нужные слова, а то, что стандартная
библиотека собирает из ленты те самые события — с теми же часами, тем же
правилом повторения и теми же исключениями.
"""

import datetime
import uuid
import zoneinfo

import icalendar
from contour import MOSCOW
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import next_tuesday
from contour import sign_in

HOUR = 3600000000


def key():
    return f'icalendar-{uuid.uuid4()}'


async def feed_of(service_client, headers):
    given = await service_client.post(
        '/api/v1/calendar-subscription',
        json={},
        headers={**headers, 'Idempotency-Key': key()},
    )
    assert given.status == 200, given.text

    feed = await service_client.get(given.json()['path'])
    assert feed.status == 200, feed.text
    return icalendar.Calendar.from_ical(feed.text)


async def test_the_feed_parses_and_names_itself(service_client, cabinet):
    """ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: лента валидна по стандарту."""
    calendar = await feed_of(service_client, await sign_in(service_client, TUTOR))

    assert calendar.get('version') == '2.0'
    assert str(calendar.get('prodid'))
    assert str(calendar.get('name'))


async def test_a_lesson_comes_back_with_the_hour_it_was_booked_for(service_client, cabinet):
    """ЧАСЫ НЕ ЕДУТ: разборщик собирает тот же час, на который занятие назначено.

    Сравнивается МЕСТНОЕ время в зоне занятия, а не момент: занятие «в 18:00 по
    Москве» обязано остаться в 18:00 у того, кто на него смотрит из Москвы.
    """
    tutor = await sign_in(service_client, TUTOR)
    starts_at = in_a_week()
    booked = await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': starts_at,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    assert booked.status == 200, booked.text

    calendar = await feed_of(service_client, tutor)

    events = [part for part in calendar.walk('VEVENT')]
    assert events, 'разборщик не нашёл ни одного события'
    found = next(
        event for event in events if str(event.get('uid')) == f'lesson-{booked.json()["id"]}@pdr'
    )

    expected = datetime.datetime.fromtimestamp(
        starts_at / 1_000_000, tz=datetime.timezone.utc
    ).astimezone(zoneinfo.ZoneInfo(MOSCOW))
    began = found.decoded('dtstart').astimezone(zoneinfo.ZoneInfo(MOSCOW))

    assert began == expected, 'разобранное время занятия не совпало с назначенным'
    assert found.decoded('dtend') - found.decoded('dtstart') == datetime.timedelta(hours=1)


async def test_the_zone_of_the_feed_is_described_for_the_parser(service_client, cabinet):
    """VTIMEZONE ЕСТЬ И РАЗБИРАЕТСЯ. Без него зона — имя, о котором календарь думает своё."""
    tutor = await sign_in(service_client, TUTOR)
    await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': in_a_week(),
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )

    calendar = await feed_of(service_client, tutor)

    zones = [part for part in calendar.walk('VTIMEZONE')]
    assert zones, 'зона названа событием, а самой зоны в ленте нет'
    assert str(zones[0].get('tzid')) == MOSCOW

    parts = [part for part in zones[0].walk() if part.name in ('STANDARD', 'DAYLIGHT')]
    assert parts, 'у зоны нет ни одного описанного куска'
    for part in parts:
        assert part.get('tzoffsetfrom') is not None
        assert part.get('tzoffsetto') is not None
        assert part.get('dtstart') is not None


async def test_a_series_comes_back_as_a_rule_the_parser_understands(service_client, cabinet):
    """СЕРИЯ УЕЗЖАЕТ ПРАВИЛОМ, и правило это разбирается, а не лежит строкой."""
    tutor = await sign_in(service_client, TUTOR)
    created = await service_client.post(
        '/api/v1/lesson-series',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'rrule': 'FREQ=WEEKLY;INTERVAL=1;BYDAY=TU;COUNT=8',
            'starts_on': next_tuesday(),
            'at': '18:00',
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    assert created.status == 200, created.text

    calendar = await feed_of(service_client, tutor)

    events = [
        event
        for event in calendar.walk('VEVENT')
        if str(event.get('uid')) == f'series-{created.json()["id"]}@pdr'
    ]
    assert events, 'серии в ленте нет'

    rule = events[0].get('rrule')
    assert rule is not None, 'серия уехала развёрткой вместо правила'
    assert rule['FREQ'] == ['WEEKLY']
    assert rule['BYDAY'] == ['TU']
    assert rule['COUNT'] == [8]

    began = events[0].decoded('dtstart').astimezone(zoneinfo.ZoneInfo(MOSCOW))
    assert began.hour == 18, 'час серии разобрался не тем, каким его назначили'
    assert began.weekday() == 1, 'первое вхождение серии встало не во вторник'
