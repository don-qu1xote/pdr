"""Контур первого процесса ПДР.

Оснастка общая с остальными наборами, которые поднимают этот же процесс, и лежит
в tests/contour.py: два представления об одной базе разошлись бы молча, оставив
оба набора зелёными.
"""

import pathlib
import sys

import pytest
import pytest_userver.utils.coverage

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests'))

pytest_plugins = ['pytest_userver.plugins.postgresql', 'contour']

UNCOVERED = {
    'мёртвый код: таблицы tariffs в схеме нет вовсе, и адаптер не собран ни в '
    'один процесс (docs/architecture/context-map.md, «Известные расхождения с кодом»)': {
        'tariff_find_by_code',
    },
    'непроверенный путь: адаптер собран и проверен своим набором, но ни одна ручка '
    'и ни одно задание единственного процесса его не зовут '
    '(docs/architecture/first-service.md)': {
        'identity_access_log_about_person',
        'identity_access_log_record',
        'identity_account_by_id',
        'identity_account_by_mail',
        'identity_account_save',
        'identity_credential_by_person',
        'identity_credential_save',
        'identity_guardian_consent_save',
        'identity_guardianship_guardians_of',
        'identity_guardianship_insert',
        'identity_guardianship_update_active',
        'identity_one_time_token_find',
        'identity_one_time_token_issue',
        'identity_one_time_token_live_invitation',
        'identity_one_time_token_mark_used',
        'identity_person_enrol',
        'identity_person_knows_mail',
        'identity_role_assignment_grant',
        'identity_session_revoke_all',
        'identity_signup_attempt_save',
        'identity_signup_attempt_window',
        'identity_tenant_open',
        'identity_tenant_visibility',
        'identity_tenant_visibility_save',
        'jobs_run_last',
        'observability_product_event_record',
    },
    'непроверенный путь: правила отмены, переноса и неявки написаны и проверены '
    'доменом и живым набором, но ручек под них в этом процессе ещё нет — звать '
    'эти запросы снаружи пока нечем (PDR-SCHED-05)': {
        'scheduling_lesson_history_add',
        'scheduling_lesson_history_of',
        'scheduling_lesson_move',
        'scheduling_lesson_set_state',
        'scheduling_series_exception_record',
        'scheduling_series_exceptions_of',
        'scheduling_series_find',
        'scheduling_series_participants_of',
    },
}
"""Запросы, которых этот прогон не выполняет, — поимённо и с причиной.

Список не оправдание, а РАВЕНСТВО: непокрытое обязано совпасть с ним в точности.
Появился новый непокрытый запрос — прогон красный, и в отчёте написано, какой
именно. Покрылся названный здесь — прогон тоже красный: список устарел, и его
надо укоротить. Расти он не должен: каждая строка отсюда уходит вместе с ручкой,
которая доводит запрос до базы.
"""

NAMED = {name for names in UNCOVERED.values() for name in names}


@pytest.fixture
def on_uncovered(sql_coverage):
    """Отчёт о непокрытых запросах — против названного списка, а не молча."""

    def _on_uncovered(uncovered_statements: set):
        surprise = sorted(uncovered_statements - NAMED)
        stale = sorted(NAMED & sql_coverage.covered_statements)
        if not surprise and not stale:
            return

        trouble = []
        if surprise:
            trouble.append(
                'Запросы, которых не выполнил ни один тест и которых нет в списке '
                f'известных: {", ".join(surprise)}. Либо доведите путь до базы '
                'тестом, либо назовите причину в UNCOVERED.',
            )
        if stale:
            trouble.append(
                'Список известных непокрытых устарел — эти запросы уже выполняются: '
                f'{", ".join(stale)}. Уберите их из UNCOVERED.',
            )
        raise pytest_userver.utils.coverage.UncoveredError('\n'.join(trouble))

    return _on_uncovered
