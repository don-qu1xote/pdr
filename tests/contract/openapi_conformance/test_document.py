"""Сам документ: он валиден, он один, и сервис отдаёт именно его."""

import openapi_spec_validator

import conformance


def test_specification_is_valid_openapi(specification):
    """Линтер, а не наше мнение о том, что документ правильный.

    Проверяет штатный валидатор OpenAPI 3.1 — тот же, которым пользуются
    инструменты порождения клиентов. Своей проверки формата у нас нет и не будет:
    она разошлась бы со стандартом на первой же его правке.
    """
    openapi_spec_validator.validate(specification)


async def test_service_serves_the_same_document(service_client, specification):
    """ДОКУМЕНТ ОДИН. Ручка меняет запись, а не содержание.

    Сверка глубокая, а не «оба разбираются»: перевод YAML в JSON — то место, где
    неоднозначный скаляр (`'true'`, `''`) молча меняет тип, и клиент получает
    схему, которой в спецификации нет.
    """
    response = await service_client.get('/api/v1/openapi.json')

    served = conformance.matches(
        specification, response, '/api/v1/openapi.json', 'GET', 200,
    )
    assert served == specification, 'выданный документ не совпал с docs/api/openapi.yaml'


def test_error_codes_are_an_enumeration(specification):
    """Коды отказа — перечисление, а не свободная строка.

    Свободная строка означает, что клиент сравнивает `type` с тем, что однажды
    прочитал в логе, а мы переименовываем коды не спросив.
    """
    problem = specification['components']['schemas']['Problem']
    code = conformance.resolved(specification, problem['properties']['type'])

    assert 'enum' in code, 'у кода отказа нет перечисления'
    assert code['enum'], 'перечисление кодов пусто'
    for value in code['enum']:
        assert value.startswith('urn:pdr:error:'), f'код «{value}» не опознаватель рода отказа'


def test_every_refusal_uses_the_one_problem_schema(specification):
    """Схема отказа ОДНА на все ручки.

    Копия схемы по ручке расходится с оригиналом в тот день, когда правку внесли
    в одну, и клиент начинает разбирать два формата вместо одного.
    """
    for path, described in specification['paths'].items():
        for method, operation in described.items():
            if method not in ('get', 'post', 'put', 'patch', 'delete'):
                continue
            for status, answer in operation['responses'].items():
                if not status.startswith(('4', '5')):
                    continue
                content = conformance.resolved(specification, answer).get('content', {})
                if conformance.MEDIA_PROBLEM not in content:
                    continue
                schema = content[conformance.MEDIA_PROBLEM]['schema']
                assert schema == {'$ref': '#/components/schemas/Problem'}, (
                    f'{method.upper()} {path} {status}: своя схема отказа вместо общей'
                )


def test_headers_of_the_contract_are_described(specification):
    """Idempotency-Key и X-Request-Id описаны явно, а не подразумеваются."""
    parameters = specification['components']['parameters']

    key = parameters['IdempotencyKey']
    assert key['in'] == 'header' and key['required'] is True, (
        'ключ повтора описан необязательным: тогда его не пришлют, и повтор '
        'выполнит операцию второй раз'
    )

    trace = parameters['RequestId']
    assert trace['in'] == 'header'

    signing = specification['paths']['/api/v1/cabinet/{tenant}/sign-in']['post']
    named = {parameter['$ref'] for parameter in signing['parameters'] if '$ref' in parameter}
    assert '#/components/parameters/IdempotencyKey' in named
    assert '#/components/parameters/RequestId' in named
