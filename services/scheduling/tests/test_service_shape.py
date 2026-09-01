"""Форма процесса: что он отдаёт наружу и чего не отдаёт.

Проверки здесь не про предметную область, а про контур: служебные ручки живут на
своём порту, метрики собираются, а секретов ни в журнале, ни в метриках нет.
Список запрещённых подстрок берётся из scripts/check_secrets.py, а не из головы.
"""

import pathlib
import subprocess  # контур-ok: список запрещённых подстрок отдаёт проверка, а не тест
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]


def forbidden_substrings():
    said = subprocess.run(  # контур-ok: сервис здесь ни при чём, спрашиваем проверку
        [sys.executable, str(ROOT / 'scripts' / 'check_secrets.py'), '--forbidden'],
        capture_output=True,
        text=True,
        check=True,
    )
    return [line for line in said.stdout.splitlines() if line.strip()]


async def test_monitor_handlers_are_not_on_the_client_port(service_client):
    """Служебная ручка снаружи не отвечает ни при какой ошибке в балансере."""
    response = await service_client.get('/service/monitor')

    assert response.status == 404


async def test_metrics_carry_the_job(monitor_client):
    """Задание видно метрикой, и метрика знает, какое именно.

    Молчащее задание — это не отсутствие событий, а поломка, и заметить её можно
    только по счётчику: «ничего не произошло» и «ничего не работает» выглядят
    одинаково во всём, кроме метрики.
    """
    metrics = await monitor_client.metrics(prefix='jobs')

    values = metrics.get('jobs.enabled')
    assert values, 'метрики задания нет вовсе'
    assert any(value.labels.get('job') == 'heartbeat-job' for value in values)


async def test_nothing_secret_leaks_into_metrics(monitor_client):
    said = await monitor_client.get('/service/monitor', params={'format': 'prometheus'})

    assert said.status == 200
    for secret in forbidden_substrings():
        assert secret not in said.text, f'в метриках нашлось запрещённое: {secret}'
