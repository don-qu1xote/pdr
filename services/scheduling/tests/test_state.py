"""Живость и готовность — РАЗНЫЕ вопросы, и отвечают на них разные ручки."""


async def test_health_answers(service_client):
    response = await service_client.get('/health')

    assert response.status == 200
    assert response.json() == {'alive': True}


async def test_readiness_sees_applied_migrations(service_client):
    response = await service_client.get('/readiness')

    assert response.status == 200
    body = response.json()
    assert body['ready'] is True
    assert body['migrations'] > 0


async def test_ping_stays_standard(service_client):
    """Штатная ручка на месте: /health и /readiness написаны ПОВЕРХ неё."""
    response = await service_client.get('/ping')

    assert response.status == 200


async def test_health_answers_while_the_database_is_gone(service_client, pgsql):
    """ГЛАВНАЯ ПРОВЕРКА ПАРЫ.

    База недоступна — процесс жив и обязан это сказать; трафика на него давать
    при этом нельзя. Общая ручка ответила бы «нет» на оба вопроса сразу и лишила
    бы оркестратор выбора: он перезапустил бы процесс, потеряв вместе с ним логи
    и метрики того, кто мог объяснить, что случилось.

    Недоступность делается не гашением базы, а отзывом прав у роли: гасить общий
    для всех тестов сервер значило бы ронять соседние прогоны.
    """
    pgsql['pdr'].cursor().execute('alter table schema_version rename to schema_version_hidden')
    try:
        alive = await service_client.get('/health')
        assert alive.status == 200
        assert alive.json() == {'alive': True}

        ready = await service_client.get('/readiness')
        assert ready.status == 503
        assert ready.json()['ready'] is False
    finally:
        pgsql['pdr'].cursor().execute(
            'alter table schema_version_hidden rename to schema_version',
        )
