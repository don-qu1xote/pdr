"""Форма процесса: что он отдаёт наружу и чего не отдаёт.

Проверки здесь не про предметную область, а про контур: служебные ручки живут на
своём порту, метрики собираются, а секретов ни в журнале, ни в метриках нет.
Список запрещённых подстрок берётся из scripts/check_secrets.py, а не из головы.
"""

import json
import pathlib
import subprocess  # контур-ok: список запрещённых подстрок отдаёт проверка, а не тест
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
CONFIGS = ROOT / 'services' / 'scheduling' / 'configs'

DEADLINE_MS = 5000
DIRECTION = {
    'attempts': 3,
    'retry_max_tokens': 100,
    'retry_token_ratio': 0.1,
    'rate_per_second': 20,
    'rate_burst': 40,
}


def started_with(tmp_path, get_free_port, service_binary, timeout_ms):
    """Запуск процесса с ОДНИМ изменённым числом: сроком единственного направления.

    Настройка берётся та же, что в установке (`services/scheduling/configs`), и
    подменяются в ней только пути да порты. Иначе проверялся бы не старт
    процесса, а собранная тестом выдумка.
    """
    fallback = json.loads(
        (CONFIGS / 'dynamic_config_fallback.json').read_text(encoding='utf-8')
    )
    fallback['PDR_REQUEST_DEADLINE'] = DEADLINE_MS
    fallback['PDR_OUTGOING_CALLS'] = {'payments': {'timeout_ms': timeout_ms, **DIRECTION}}
    (tmp_path / 'fallback.json').write_text(json.dumps(fallback), encoding='utf-8')
    (tmp_path / 'dumps').mkdir()

    variables = (CONFIGS / 'config_vars.yaml').read_text(encoding='utf-8')
    for was, now in {
        '/etc/pdr/secrets.json': str(CONFIGS / 'secdist.json'),
        '/etc/pdr/dynamic_config_fallback.json': str(tmp_path / 'fallback.json'),
        '/var/cache/pdr/dynamic_config.json': str(tmp_path / 'dynamic_config.json'),
        '/etc/pdr/openapi.yaml': str(ROOT / 'docs' / 'api' / 'openapi.yaml'),
        '/var/cache/pdr/dumps': str(tmp_path / 'dumps'),
        'server-port: 8080': f'server-port: {get_free_port()}',
        'monitor-port: 8081': f'monitor-port: {get_free_port()}',
    }.items():
        variables = variables.replace(was, now)
    (tmp_path / 'config_vars.yaml').write_text(variables, encoding='utf-8')

    said = subprocess.run(  # контур-ok: ждём, что процесс НЕ поднимется, — плагин такого не умеет
        [
            str(service_binary),
            '--config',
            str(CONFIGS / 'static_config.yaml'),
            '--config_vars',
            str(tmp_path / 'config_vars.yaml'),
        ],
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    return said.returncode, said.stdout + said.stderr


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


async def test_every_outgoing_direction_is_visible_in_metrics(monitor_client):
    """Каждое направление наружу видно метрикой, и метрика знает, какое именно.

    «У них там что-то медленно» без метрики остаётся ощущением: доля отказов и
    время ответа считаются штатным клиентом по направлению
    (httpclient.destinations.<имя>), а срабатывания бюджета повторов — самим
    бюджетом.

    Заодно это единственный способ увидеть снаружи, что проверка сроков на
    старте прошла: направления попадают в метрику только после неё, а не
    прошедшая роняет процесс — сервис бы не поднялся вовсе.
    """
    metrics = await monitor_client.metrics(prefix='outgoing')

    budgets = metrics.get('outgoing.retries.max_token_count')
    assert budgets, 'метрик направлений наружу нет вовсе'
    quotas = metrics.get('outgoing.quota.obtain_failed')
    assert quotas, 'метрики квоты нет: превышение своей же квоты останется незаметным'

    seen = {value.labels.get('direction') for value in budgets}
    for direction in ('payments', 'receipts', 'video', 'model', 'handwriting'):
        assert direction in seen, f'направления {direction} в метриках нет'


def test_a_direction_outliving_its_request_does_not_let_the_process_start(
    tmp_path, get_free_port, service_binary
):
    """Негодный срок наружу роняет процесс на старте, а не однажды в проде.

    Вызов со сроком не меньше срока запроса переживает собственный запрос:
    ответа уже никто не ждёт, а соединение занято. Оставить такую настройку
    действующей нельзя, и единственное место, где процесс имеет право отказаться
    продолжать, — старт.
    """
    code, said = started_with(tmp_path, get_free_port, service_binary, DEADLINE_MS + 1000)

    assert 'сервис не поднимается' in said, said[-2000:]
    assert 'payments' in said, 'отказ не называет направление, из-за которого он случился'
    assert code != 0, 'процесс поднялся с вызовом, переживающим собственный запрос'


def test_a_direction_inside_its_request_is_let_through(
    tmp_path, get_free_port, service_binary
):
    """Оговорка к предыдущему: годный срок старту не мешает.

    Без неё проверка была бы зелёной и в мире, где процесс не поднимается
    никогда, — а это ровно то, чего она проверять не должна. Дальше сверки
    сроков процесс в этом наборе не идёт: базы у него здесь нет, и падает он
    уже на ней.
    """
    code, said = started_with(tmp_path, get_free_port, service_binary, DEADLINE_MS - 1000)

    assert 'сервис не поднимается' not in said, said[-2000:]
    assert 'направления наружу проверены' in said, said[-2000:]
    assert code != 0, 'база в этом наборе не поднята — процесс обязан упасть на ней'


async def test_nothing_secret_leaks_into_metrics(monitor_client):
    said = await monitor_client.get('/service/monitor', params={'format': 'prometheus'})

    assert said.status == 200
    for secret in forbidden_substrings():
        assert secret not in said.text, f'в метриках нашлось запрещённое: {secret}'
