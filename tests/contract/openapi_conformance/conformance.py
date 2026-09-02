"""Как ответ сверяется со схемой из спецификации.

Одно место на весь набор: каждая проверка называет обмен, а решает, сошлось ли
оно, эта функция. Иначе через полгода один тест сверяет тело, второй ещё и
заголовки, третий — только код ответа, и «прогон зелёный» перестаёт что-либо
значить.
"""

import json

import jsonschema

MEDIA_PROBLEM = 'application/problem+json'
MEDIA_JSON = 'application/json'
MEDIA_TEXT = 'text/plain'
MEDIA_OCTETS = 'application/octet-stream'


def operation_of(specification, path, method):
    described = specification['paths'].get(path)
    assert described is not None, f'путь {path} не описан в спецификации'

    operation = described.get(method.lower())
    assert operation is not None, f'у пути {path} не описан метод {method}'
    return operation


def resolved(specification, node):
    """Ссылка `$ref`, разрешённая внутри документа.

    Разрешаются только внутренние ссылки: спецификация лежит одним файлом, а
    ссылка наружу означала бы второй источник правды.
    """
    while isinstance(node, dict) and '$ref' in node and len(node) == 1:
        reference = node['$ref']
        assert reference.startswith('#/'), f'внешняя ссылка в спецификации: {reference}'
        node = specification
        for step in reference[2:].split('/'):
            node = node[step]
    return node


def _document(specification, schema):
    """Схема вместе с разделом определений, на который она ссылается.

    Ссылки внутри схемы (`#/components/schemas/...`) разрешает сам проверяющий —
    но только внутри того документа, который ему дали. Своего разрешателя
    ссылок мы не пишем: он разошёлся бы с настоящим ровно там, где это трудно
    заметить.
    """
    document = dict(schema)
    document['components'] = specification['components']
    return document


def matches(specification, response, path, method, status, media=MEDIA_JSON, parse=None):
    """Проверить один обмен целиком: код, тип содержимого, тело и заголовки.

    `parse` нужен ровно одному ответу во всём контуре — 498. Там штатный механизм
    доведения срока ставит тип `application/json`, а телом кладёт обычный текст, и
    одно с другим не сходится. Спецификация это описывает как есть (сказать
    «application/json» и разобрать текст — единственный способ не соврать ни про
    заголовок, ни про тело), а разбирать такое тело как JSON нечем.
    """
    operation = operation_of(specification, path, method)

    assert response.status == status, (
        f'{method} {path}: ответ {response.status}, а спецификация обещает {status}. '
        f'Тело: {response.text[:400]}'
    )

    described = operation['responses'].get(str(status))
    assert described is not None, f'{method} {path}: ответ {status} не описан'
    described = resolved(specification, described)

    content_type = response.headers.get('Content-Type', '')
    assert content_type.split(';')[0].strip() == media, (
        f'{method} {path} {status}: тип содержимого «{content_type}», а описан «{media}»'
    )

    for name, header in (described.get('headers') or {}).items():
        if resolved(specification, header).get('required'):
            assert name in response.headers, (
                f'{method} {path} {status}: обещан заголовок {name}, а его нет'
            )

    schema = resolved(specification, described['content'][media]['schema'])
    as_text = parse is False or media in (MEDIA_TEXT, MEDIA_OCTETS)
    body = response.text if as_text else json.loads(response.text)

    jsonschema.Draft202012Validator(_document(specification, schema)).validate(body)
    return body
