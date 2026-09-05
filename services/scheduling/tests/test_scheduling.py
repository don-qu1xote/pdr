"""РАСПИСАНИЕ СКВОЗЬ ВСЕ СЛОИ: маршрут, форма, политика, сценарий, база.

Здесь проверяется не предметная область — её проверяют unit-прогон и
contract-набор, — а то, что слои сошлись в живом процессе и что права
спрашиваются НА КАЖДОМ обращении. Отрицательные случаи поэтому главные: ручка,
которая пускает всех, проходит любой положительный тест.

Кабинет и люди в нём — общие с набором сверки контракта (tests/contour.py):
вторая копия засыпки разошлась бы с первой молча, и оба набора остались бы
зелёными, каждый на своём представлении о том, кто заведён.
"""

import uuid

from contour import GUARDIAN
from contour import MOSCOW
from contour import OUTSIDER
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import next_tuesday
from contour import sign_in

HOUR = 3600000000
DAY = 24 * HOUR

MARCH = in_a_week()


def key():
    return f'scheduling-{uuid.uuid4()}'


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


async def test_the_tutor_books_a_lesson_and_sees_it_in_his_month(service_client, cabinet):
    """ГЛАВНЫЙ ОБМЕН: записали занятие и увидели его там, где ищут."""
    tutor = await sign_in(service_client, TUTOR)

    created = await book(service_client, tutor)
    assert created.status == 200, created.text
    lesson = created.json()
    assert lesson['tutor'] == TUTOR
    assert lesson['participants'] == [STUDENT]
    assert lesson['ends_at'] == MARCH + HOUR
    assert lesson['state'] == 'planned'
    assert lesson['tz'] == MOSCOW

    found = await service_client.get(
        '/api/v1/lessons',
        params={'from': MARCH - DAY, 'to': MARCH + 30 * DAY, 'side': 'tutor'},
        headers=tutor,
    )

    assert found.status == 200, found.text
    assert [one['id'] for one in found.json()['lessons']] == [lesson['id']]


async def test_the_same_key_does_not_book_a_second_lesson(service_client, cabinet):
    """ПОВТОР ПО ТОМУ ЖЕ КЛЮЧУ отдаёт сохранённый ответ, а не пишет второе."""
    people = cabinet
    tutor = await sign_in(service_client, TUTOR)

    used = key()
    first = await book(service_client, tutor, used=used)
    assert first.status == 200, first.text

    again = await book(service_client, tutor, used=used)

    assert again.status == 200
    assert again.json() == first.json(), 'повтор ответил не тем же самым'
    assert again.headers['Idempotency-Replayed'] == 'true'
    assert people.lessons() == 1, 'повтор по тому же ключу записал второе занятие'


async def test_a_mutating_request_without_a_key_is_refused(service_client, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    response = await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': MARCH,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers=tutor,
    )

    assert response.status == 400
    assert response.json()['type'] == 'urn:pdr:error:idempotency_key_required'


async def test_the_student_sees_his_own_lessons(service_client, cabinet):
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    student = await sign_in(service_client, STUDENT)
    found = await service_client.get(
        '/api/v1/lessons',
        params={'from': MARCH - DAY, 'to': MARCH + DAY, 'side': 'participant'},
        headers=student,
    )

    assert found.status == 200, found.text
    assert len(found.json()['lessons']) == 1


async def test_the_student_does_not_see_someone_elses_lessons(service_client, cabinet):
    """ОТРИЦАТЕЛЬНЫЙ ПО ПОЛИТИКЕ: чужое расписание не открывается.

    Отказывает политика, а не сценарий: до базы дело не доходит вовсе, и по коду
    ответа нельзя узнать даже того, есть ли у соседа занятия.
    """
    outsider = await sign_in(service_client, OUTSIDER)

    refused = await service_client.get(
        '/api/v1/lessons',
        params={
            'from': MARCH - DAY,
            'to': MARCH + DAY,
            'side': 'participant',
            'whose': STUDENT,
        },
        headers=outsider,
    )

    assert refused.status == 403, refused.text
    assert refused.headers['Content-Type'] == 'application/problem+json'
    assert refused.json()['type'] == 'urn:pdr:error:not_yours'


async def test_a_guardian_without_the_scope_is_refused(service_client, cabinet):
    """ОТРИЦАТЕЛЬНЫЙ ПО ПОЛИТИКЕ: опека есть, доступа к расписанию нет.

    Опека сама по себе уровней не открывает — их открывают отдельно, и это не
    формальность: «родитель видит всё» отменило бы все остальные строки прав.
    """
    guardian = await sign_in(service_client, GUARDIAN)

    refused = await service_client.get(
        '/api/v1/lessons',
        params={
            'from': MARCH - DAY,
            'to': MARCH + DAY,
            'side': 'participant',
            'whose': STUDENT,
        },
        headers=guardian,
    )

    assert refused.status == 403, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:scope_missing'


async def test_a_guardian_with_the_scope_sees_the_ward(service_client, cabinet):
    """Оговорка к предыдущему: с открытым уровнем опекун видит.

    Без неё отказ выше был бы зелёным и в мире, где опекуна не пускают никогда.
    """
    cabinet.open_schedule_to_the_guardian()
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    guardian = await sign_in(service_client, GUARDIAN)

    found = await service_client.get(
        '/api/v1/lessons',
        params={
            'from': MARCH - DAY,
            'to': MARCH + DAY,
            'side': 'participant',
            'whose': STUDENT,
        },
        headers=guardian,
    )

    assert found.status == 200, found.text
    assert len(found.json()['lessons']) == 1


async def test_one_lesson_is_read_from_the_named_schedule(service_client, cabinet):
    tutor = await sign_in(service_client, TUTOR)
    created = await book(service_client, tutor)
    assert created.status == 200
    lesson = created.json()['id']

    found = await service_client.get(
        f'/api/v1/lessons/{lesson}', params={'side': 'tutor'}, headers=tutor,
    )

    assert found.status == 200, found.text
    assert found.json() == created.json()


async def test_someone_elses_lesson_is_not_found(service_client, cabinet):
    """Занятия вне названного расписания нет ТАК ЖЕ, как несуществующего."""
    tutor = await sign_in(service_client, TUTOR)
    created = await book(service_client, tutor)
    assert created.status == 200

    outsider = await sign_in(service_client, OUTSIDER)
    refused = await service_client.get(
        f'/api/v1/lessons/{created.json()["id"]}',
        params={'side': 'participant'},
        headers=outsider,
    )

    assert refused.status == 404, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:lesson_not_found'


async def test_availability_is_written_whole_and_read_back(service_client, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    absent = await service_client.get('/api/v1/availability', headers=tutor)
    assert absent.status == 404, absent.text
    assert absent.json()['type'] == 'urn:pdr:error:availability_not_set'

    written = await service_client.put(
        '/api/v1/availability',
        json={
            'rules': [{'weekday': 2, 'from': '10:00', 'to': '18:00', 'tz': MOSCOW}],
            'exceptions': [{'date': next_tuesday()}],
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    assert written.status == 200, written.text

    found = await service_client.get('/api/v1/availability', headers=tutor)

    assert found.status == 200, found.text
    assert found.json() == written.json()
    assert found.json()['rules'][0]['from'] == '10:00'
    assert found.json()['exceptions'][0]['date'] == next_tuesday()


async def test_only_the_tutor_himself_sets_his_hours(service_client, cabinet):
    """ОТРИЦАТЕЛЬНЫЙ ПО ПОЛИТИКЕ: «когда я работаю» за другого не скажет никто."""
    student = await sign_in(service_client, STUDENT)

    refused = await service_client.put(
        '/api/v1/availability',
        params={'whose': TUTOR},
        json={'rules': [], 'exceptions': []},
        headers={**student, 'Idempotency-Key': key()},
    )

    assert refused.status == 403, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:role_missing'


async def test_a_series_is_kept_as_a_rule(service_client, cabinet):
    """Серия заводится ПРАВИЛОМ: занятий после неё в базе не появляется."""
    people = cabinet
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
    assert created.json()['at'] == '18:00'
    assert created.json()['participants'] == [STUDENT]
    assert people.lessons() == 0, 'серия развернулась в занятия при заведении'


async def test_a_rule_outside_the_subset_is_refused_aloud(service_client, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    refused = await service_client.post(
        '/api/v1/lesson-series',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'rrule': 'FREQ=WEEKLY;BYDAY=TU;BYSETPOS=1;COUNT=8',
            'starts_on': next_tuesday(),
            'at': '18:00',
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )

    assert refused.status == 422, refused.text
    assert refused.json()['type'] == 'urn:pdr:error:recurrence_rule_unsupported'


async def test_nobody_gets_in_without_a_session(service_client, cabinet):

    refused = await service_client.get(
        '/api/v1/lessons', params={'from': MARCH, 'to': MARCH + DAY, 'side': 'tutor'},
    )

    assert refused.status == 401, refused.text
    assert refused.headers['Content-Type'] == 'application/problem+json'
