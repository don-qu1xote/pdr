"""Настоящие ответы сервиса против схем из спецификации.

Каждый обмен здесь — из тех, что клиент действительно делает. Проверяется всё
сразу: код ответа, тип содержимого, обещанные заголовки и тело по схеме.
Расхождение любого из четырёх — красный прогон, а не запись в журнале.
"""

import uuid

import conformance
from contour import CABINET as TENANT
from contour import CABINET_EMAIL as EMAIL
from contour import CABINET_PASSWORD as PASSWORD
from contour import MOSCOW
from contour import OUTSIDER
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import next_tuesday
from contour import sign_in

SIGN_IN = '/api/v1/cabinet/{tenant}/sign-in'
LESSONS = '/api/v1/lessons'
ONE_LESSON = '/api/v1/lessons/{lesson}'
SERIES = '/api/v1/lesson-series'
AVAILABILITY = '/api/v1/availability'


def address(tenant=TENANT):
    return SIGN_IN.replace('{tenant}', tenant)


def key():
    return f'sign-in-{uuid.uuid4()}'


async def test_ping_answers_as_described(service_client, specification):
    response = await service_client.get('/ping')

    conformance.matches(
        specification, response, '/ping', 'GET', 200, conformance.MEDIA_OCTETS,
    )


async def test_health_answers_as_described(service_client, specification):
    response = await service_client.get('/health')

    body = conformance.matches(specification, response, '/health', 'GET', 200)
    assert body['alive'] is True


async def test_readiness_answers_as_described(service_client, specification):
    response = await service_client.get('/readiness')

    body = conformance.matches(specification, response, '/readiness', 'GET', 200)
    assert body['ready'] is True


async def test_sign_in_answers_as_described(service_client, specification, practice):
    """ГЛАВНЫЙ ОБМЕН: вход прошёл, сессия выдана заголовком, а не полем тела."""
    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': PASSWORD},
        headers={'Idempotency-Key': key()},
    )

    body = conformance.matches(specification, response, SIGN_IN, 'POST', 200)
    assert body['expires_at'] > 0
    assert '__Host-pdr_session=' in response.headers['Set-Cookie']


async def test_refusal_answers_as_described(service_client, specification, practice):
    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': 'не тот'},
        headers={'Idempotency-Key': key()},
    )

    problem = conformance.matches(
        specification, response, SIGN_IN, 'POST', 422, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:sign_in_refused'


async def test_missing_key_answers_as_described(service_client, specification, practice):
    response = await service_client.post(
        address(), json={'email': EMAIL, 'password': PASSWORD},
    )

    problem = conformance.matches(
        specification, response, SIGN_IN, 'POST', 400, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:idempotency_key_required'


async def test_body_outside_the_schema_answers_as_described(
    service_client, specification, practice,
):
    response = await service_client.post(
        address(), json={'email': EMAIL}, headers={'Idempotency-Key': key()},
    )

    problem = conformance.matches(
        specification, response, SIGN_IN, 'POST', 400, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:request_field_invalid'
    assert 'field' in problem


async def test_unknown_cabinet_answers_as_described(service_client, specification):
    response = await service_client.post(
        address(tenant='не-адрес'),
        json={'email': EMAIL, 'password': PASSWORD},
        headers={'Idempotency-Key': key()},
    )

    problem = conformance.matches(
        specification, response, SIGN_IN, 'POST', 401, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:cabinet_unknown'


async def test_an_expired_deadline_answers_as_described(
    service_client, specification, practice,
):
    """ОБЯЗАТЕЛЬНЫЙ ОБМЕН ЗАДАЧИ: срок вышел — 498, и работа не начиналась.

    Срок клиент называет сам, заголовком `X-YaTaxi-Client-TimeoutMs`. Ноль
    означает «времени нет вовсе»: ответ обязан прийти сразу и до всякой работы.

    Отвечает штатный механизм, а не наша форма: тело здесь не problem+json, а
    тип содержимого с ним даже не сходится — `application/json` при обычном
    тексте. И то и другое названо в спецификации, а не спрятано.

    Что транзакция при этом не открывалась, видно отсюда косвенно — по ключу
    повтора. Ключ тот же самый используется следом, и второе обращение обязано
    пройти как ПЕРВОЕ, а не как повтор: занятый ключ означал бы, что просроченный
    запрос успел открыть область и занять строку.
    """
    reused = key()
    body = {'email': EMAIL, 'password': PASSWORD}

    response = await service_client.post(
        address(),
        json=body,
        headers={'Idempotency-Key': reused, 'X-YaTaxi-Client-TimeoutMs': '0'},
    )

    conformance.matches(
        specification, response, SIGN_IN, 'POST', 498, conformance.MEDIA_JSON, parse=False,
    )

    again = await service_client.post(
        address(), json=body, headers={'Idempotency-Key': reused},
    )
    assert again.status == 200, 'ключ остался занят: просроченный запрос всё-таки открыл область'
    assert 'Idempotency-Replayed' not in again.headers, (
        'ответ пришёл сохранённым: просроченный запрос успел записать его'
    )


async def test_repeat_returns_the_saved_answer(service_client, specification, practice):
    """ПОВТОР ОТДАЁТ СОХРАНЁННОЕ, а не выполняет вход второй раз.

    Отличить повтор от первого раза клиент может по заголовку — он для того и
    описан в спецификации.
    """
    repeated = key()
    body = {'email': EMAIL, 'password': PASSWORD}

    first = await service_client.post(
        address(), json=body, headers={'Idempotency-Key': repeated},
    )
    assert first.status == 200

    response = await service_client.post(
        address(), json=body, headers={'Idempotency-Key': repeated},
    )

    answer = conformance.matches(specification, response, SIGN_IN, 'POST', 200)
    assert answer == first.json(), 'повтор отдал не тот ответ, что был сохранён'
    assert response.headers['Idempotency-Replayed'] == 'true'


async def test_reused_key_answers_as_described(service_client, specification, practice):
    """Тот же ключ с ДРУГИМ телом — ошибка клиента, а не повтор.

    Первое обращение обязано пройти: отказавшая операция уносит строку ключа с
    собой (PDR-API-02), и после отказа тот же ключ снова свободен.
    """
    reused = key()
    first = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': PASSWORD},
        headers={'Idempotency-Key': reused},
    )
    assert first.status == 200

    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': 'совсем другой'},
        headers={'Idempotency-Key': reused},
    )

    problem = conformance.matches(
        specification, response, SIGN_IN, 'POST', 409, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:idempotency_key_reused'


async def booked(service_client, tutor):
    """Занятие, записанное тем же обменом, что и у клиента."""
    response = await service_client.post(
        LESSONS,
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': in_a_week(),
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    assert response.status == 200, response.text
    return response


async def test_a_booked_lesson_answers_as_described(service_client, specification, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    response = await booked(service_client, tutor)

    lesson = conformance.matches(specification, response, LESSONS, 'POST', 200)
    assert lesson['participants'] == [STUDENT]
    assert lesson['state'] == 'planned'


async def test_a_month_of_lessons_answers_as_described(service_client, specification, cabinet):
    tutor = await sign_in(service_client, TUTOR)
    await booked(service_client, tutor)

    response = await service_client.get(
        LESSONS,
        params={'from': in_a_week() - 86400000000, 'to': in_a_week() + 86400000000, 'side': 'tutor'},
        headers=tutor,
    )

    found = conformance.matches(specification, response, LESSONS, 'GET', 200)
    assert len(found['lessons']) == 1


async def test_one_lesson_answers_as_described(service_client, specification, cabinet):
    tutor = await sign_in(service_client, TUTOR)
    created = await booked(service_client, tutor)

    response = await service_client.get(
        ONE_LESSON.replace('{lesson}', created.json()['id']),
        params={'side': 'tutor'},
        headers=tutor,
    )

    conformance.matches(specification, response, ONE_LESSON, 'GET', 200)


async def test_a_lesson_outside_the_schedule_answers_as_described(
    service_client, specification, cabinet,
):
    """Чужое занятие не находится ТАК ЖЕ, как несуществующее."""
    tutor = await sign_in(service_client, TUTOR)
    created = await booked(service_client, tutor)

    outsider = await sign_in(service_client, OUTSIDER)
    response = await service_client.get(
        ONE_LESSON.replace('{lesson}', created.json()['id']),
        params={'side': 'participant'},
        headers=outsider,
    )

    problem = conformance.matches(
        specification, response, ONE_LESSON, 'GET', 404, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:lesson_not_found'


async def test_availability_answers_as_described(service_client, specification, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    absent = await service_client.get(AVAILABILITY, headers=tutor)
    problem = conformance.matches(
        specification, absent, AVAILABILITY, 'GET', 404, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:availability_not_set'

    written = await service_client.put(
        AVAILABILITY,
        json={
            'rules': [{'weekday': 2, 'from': '10:00', 'to': '18:00', 'tz': MOSCOW}],
            'exceptions': [],
        },
        headers={**tutor, 'Idempotency-Key': key()},
    )
    conformance.matches(specification, written, AVAILABILITY, 'PUT', 200)

    response = await service_client.get(AVAILABILITY, headers=tutor)

    found = conformance.matches(specification, response, AVAILABILITY, 'GET', 200)
    assert found['rules'][0]['to'] == '18:00'


async def test_a_series_answers_as_described(service_client, specification, cabinet):
    tutor = await sign_in(service_client, TUTOR)

    response = await service_client.post(
        SERIES,
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

    series = conformance.matches(specification, response, SERIES, 'POST', 200)
    assert series['rrule'].startswith('FREQ=WEEKLY')


async def test_a_refusal_by_policy_answers_as_described(service_client, specification, cabinet):
    """403 описан у каждой ручки расписания, и приходит он той же формой."""
    student = await sign_in(service_client, STUDENT)

    response = await service_client.put(
        AVAILABILITY,
        params={'whose': TUTOR},
        json={'rules': [], 'exceptions': []},
        headers={**student, 'Idempotency-Key': key()},
    )

    problem = conformance.matches(
        specification, response, AVAILABILITY, 'PUT', 403, conformance.MEDIA_PROBLEM,
    )
    assert problem['type'] == 'urn:pdr:error:role_missing'
