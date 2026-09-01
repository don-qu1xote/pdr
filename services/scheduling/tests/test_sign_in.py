"""ВХОД СКВОЗЬ ВСЕ СЛОИ.

Одна работающая ручка — доказательство того, что фундамент собирается в живой
процесс: маршрут, звенья конвейера, форма запроса, схема тела, ключ повтора,
область арендатора, сценарий, адаптеры и настоящий счёт пароля Argon2id.

Ручка входа — единственная дверь: сессии у запроса ещё нет, он её создаёт.
Арендатор поэтому берётся из адреса кабинета.
"""

import uuid

import pytest

TENANT = '11111111-1111-4111-8111-111111111111'
PERSON = '22222222-2222-4222-8222-222222222222'
EMAIL = 'nina@example.org'
PASSWORD = 'correct-horse-battery'


def address(path=''):
    return f'/api/v1/cabinet/{TENANT}/sign-in{path}'


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
    return rows


async def test_wrong_password_is_refused_in_one_shape(service_client, practice):
    """Отказ приходит той же формой, что и все остальные: problem+json."""
    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': 'не тот'},
        headers={'Idempotency-Key': f'sign-in-{uuid.uuid4()}'},
    )

    assert response.status == 422, 'статус выбран не по таблице родов отказа'
    assert response.headers['Content-Type'] == 'application/problem+json'

    problem = response.json()
    assert problem['type'] == 'urn:pdr:error:sign_in_refused'
    assert problem['status'] == 422
    assert problem['instance'] == address()
    assert problem['request_id']
    assert response.headers['X-Request-Id'] == problem['request_id']


async def test_security_headers_are_on_the_refusal_too(service_client, practice):
    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': 'не тот'},
        headers={'Idempotency-Key': f'sign-in-{uuid.uuid4()}'},
    )

    assert response.headers['X-Content-Type-Options'] == 'nosniff'
    assert response.headers['Referrer-Policy'] == 'no-referrer'
    assert 'frame-ancestors' in response.headers['Content-Security-Policy']
    assert 'camera=()' in response.headers['Permissions-Policy']


async def test_key_is_required(service_client, practice):
    """Заголовок повтора обязателен у меняющего обращения, а не желателен."""
    response = await service_client.post(
        address(),
        json={'email': EMAIL, 'password': PASSWORD},
    )

    assert response.status == 400
    assert response.json()['type'] == 'urn:pdr:error:idempotency_key_required'


async def test_body_is_checked_against_the_schema(service_client, practice):
    response = await service_client.post(
        address(),
        json={'email': EMAIL},
        headers={'Idempotency-Key': f'sign-in-{uuid.uuid4()}'},
    )

    assert response.status == 400
    problem = response.json()
    assert problem['type'] == 'urn:pdr:error:request_field_invalid'


async def test_unknown_cabinet_is_not_a_way_in(service_client):
    response = await service_client.post(
        '/api/v1/cabinet/не-адрес/sign-in',
        json={'email': EMAIL, 'password': PASSWORD},
        headers={'Idempotency-Key': f'sign-in-{uuid.uuid4()}'},
    )

    assert response.status == 401
    assert response.json()['type'] == 'urn:pdr:error:cabinet_unknown'
