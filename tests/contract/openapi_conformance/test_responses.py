"""Настоящие ответы сервиса против схем из спецификации.

Каждый обмен здесь — из тех, что клиент действительно делает. Проверяется всё
сразу: код ответа, тип содержимого, обещанные заголовки и тело по схеме.
Расхождение любого из четырёх — красный прогон, а не запись в журнале.
"""

import uuid

import pytest

import conformance

TENANT = '11111111-1111-4111-8111-111111111111'
PERSON = '22222222-2222-4222-8222-222222222222'
EMAIL = 'nina@example.org'
PASSWORD = 'correct-horse-battery'

SIGN_IN = '/api/v1/cabinet/{tenant}/sign-in'

PASSWORD_HASH = (
    '$argon2id$v=19$m=65536,t=3,p=1$cGRyLWNvbnRyYWN0LXNsdA'
    '$XGIFHfMENSnGu6lfQYEW/Ikhm2vP48/FF5k9G+yAnmg'
)
"""Argon2id от PASSWORD, посчитанный один раз и записанный сюда.

Параметры счёта лежат ВНУТРИ записи, поэтому проверка не зависит ни от текущего
PDR_SIGN_IN_RULES, ни от того, чем хеш посчитали. Считать его в прогоне значило
бы тратить на каждый тест десятки миллисекунд ровно ради того, что и так
проверено contract-набором счёта паролей.
"""


def address(tenant=TENANT):
    return SIGN_IN.replace('{tenant}', tenant)


def key():
    return f'sign-in-{uuid.uuid4()}'


@pytest.fixture
def practice(pgsql):
    rows = pgsql['pdr'].cursor()
    rows.execute(
        "insert into identity_tenant (tenant_id, name, tz) values (%s, 'Нина', 'Europe/Moscow') "
        'on conflict do nothing',
        (TENANT,),
    )
    rows.execute(
        'insert into identity_person (tenant_id, id, display_name, email, tz) '
        "values (%s, %s, 'Нина', %s, 'Europe/Moscow') on conflict do nothing",
        (TENANT, PERSON, EMAIL),
    )
    rows.execute(
        'insert into identity_credential (tenant_id, person_id, password_hash) '
        'values (%s, %s, %s) on conflict do nothing',
        (TENANT, PERSON, PASSWORD_HASH),
    )
    return rows


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
