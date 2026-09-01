"""Сверка настоящих ответов сервиса со спецификацией.

Набор поднимает ТОТ ЖЕ процесс, что и сценарии сервиса, и берёт ту же оснастку
(tests/contour.py). Отдельный он не ради изоляции, а ради предмета: сценарии
проверяют, что сервис делает что нужно, а этот — что он отвечает так, как
обещано клиенту.

БЕЗ ЭТОГО НАБОРА СПЕЦИФИКАЦИЯ УСТАРЕЕТ ЗА ДВЕ НЕДЕЛИ. Это происходит всегда:
документ, который никто не сверяет с работающим процессом, расходится с ним
первым же изменением ответа — и расходится молча.
"""

import pathlib
import sys

import pytest
import yaml

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests'))

pytest_plugins = ['pytest_userver.plugins.postgresql', 'contour']

SPEC = ROOT / 'docs' / 'api' / 'openapi.yaml'


@pytest.fixture(scope='session')
def specification():
    """Спецификация, разобранная НАСТОЯЩИМ разборщиком YAML.

    Тем же, которым её прочитает инструмент порождения клиентов, — а не нашим
    представлением о том, как она разбирается.
    """
    return yaml.safe_load(SPEC.read_text(encoding='utf-8'))
