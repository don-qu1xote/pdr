"""ЖУРНАЛ ПОЛЯМИ: что видно в записи поднятого процесса.

Здесь и только здесь проверяются ТЕГИ СПАНА. Ловец записей из unit-набора
(`utest::LogCaptureLogger`) собирает запись сам и тегов в неё не переносит —
даже штатного `trace_id`; тест на нём проверял бы устройство ловца, а не
постановку тега. Настоящую запись собирает настоящий журнал, и добраться до неё
можно ровно одним штатным способом — `service_client.capture_logs()`.

Второй вопрос набора — тот, ради которого реестр полей вообще написан: ни одно
поле из перечня ПДн (PDR-SEC-05) в журнал не попадает. Поля удобнее текста ровно
тем же, чем опаснее.
"""

import pathlib
import re
import subprocess  # контур-ok: список запрещённых имён отдаёт проверка, а не тест
import sys
import uuid

from contour import CABINET as TENANT
from contour import CABINET_EMAIL as EMAIL
from contour import CABINET_PASSWORD as PASSWORD
from contour import PARTICIPANT

ROOT = pathlib.Path(__file__).resolve().parents[3]

TENANT_FIELD = 'tenant_id'
ACTOR_FIELD = 'actor_id'
ZERO_ID = '00000000-0000-0000-0000-000000000000'


def forbidden_field_names():
    """Имена полей, запрещённых перечнем ПДн. Спрашиваются у самой проверки."""
    said = subprocess.run(  # контур-ok: перечень держит проверка, а не тест
        [sys.executable, str(ROOT / 'scripts' / 'check_log_fields.py'), '--forbidden'],
        capture_output=True,
        text=True,
        check=True,
    )
    return [line for line in said.stdout.splitlines() if line.strip()]


def address():
    return f'/api/v1/cabinet/{TENANT}/sign-in'


async def sign_in(service_client):
    return await service_client.post(
        address(),
        json={'email': EMAIL, 'password': PASSWORD},
        headers={'Idempotency-Key': f'journal-{uuid.uuid4()}'},
    )


async def test_the_record_carries_the_tenant(service_client, practice):
    """Арендатор есть в записи, а не в тексте.

    Модель построена на `tenant_id` целиком: RLS, изоляция, все проверки.
    Запись без него ищется грепом по русским строкам, и это ровно тот способ,
    которым разбирают первый инцидент те, кто не завёл поля заранее.
    """
    async with service_client.capture_logs() as capture:
        response = await sign_in(service_client)
        assert response.status == 200

    tagged = capture.select(**{TENANT_FIELD: TENANT})

    assert tagged, 'ни одной записи с полем арендатора не нашлось'


async def test_the_door_has_no_actor_and_says_so_by_silence(service_client, practice):
    """У ДВЕРИ АКТОРА НЕТ, и поля тоже нет.

    Входящий становится человеком по итогу того самого запроса, которым входит:
    `Caller::actor` у `DoorHandler` пуст намеренно. Ноль в поле хуже отсутствия
    поля — по нему ищут и находят все входы разом, приняв их за один.

    Поле `actor_id` появится с первой ручкой ЗА дверью; сегодня в дереве такой
    ручки нет ни одной, и притворяться, что есть, этот набор не станет.
    """
    async with service_client.capture_logs() as capture:
        response = await sign_in(service_client)
        assert response.status == 200

    assert not capture.select(**{ACTOR_FIELD: PARTICIPANT})
    assert not capture.select(**{ACTOR_FIELD: ZERO_ID}), "ноль актора всё-таки попал в поле"


async def test_nothing_from_the_personal_data_list_becomes_a_field(service_client, practice):
    """ПОЛЯ УДОБНЕЕ ТЕКСТА РОВНО ТЕМ ЖЕ, ЧЕМ ОПАСНЕЕ.

    По `email=...` ищут так же легко, как по `tenant_id=...`, — и выгружают так
    же легко. Перечень обещает человеку срок хранения по каждой категории, а
    журнал живёт своим сроком и переживёт обещание молча.

    Проверяются ИМЕНА полей, а не значения: значение бывает любым, а имя — это
    решение, и принимается оно один раз.
    """
    async with service_client.capture_logs() as capture:
        assert (await sign_in(service_client)).status == 200
        wrong = await service_client.post(
            address(),
            json={'email': EMAIL, 'password': 'не тот пароль'},
            headers={'Idempotency-Key': f'journal-{uuid.uuid4()}'},
        )
        assert wrong.status == 422

    forbidden = [re.compile(pattern) for pattern in forbidden_field_names()]
    assert forbidden, 'проверка не назвала ни одного запрещённого имени'

    for record in capture.select():
        for field in record:
            for pattern in forbidden:
                assert not pattern.search(field), (
                    f'в журнале завелось поле «{field}»: имя из перечня ПДн '
                    f'(docs/legal/personal-data.md)'
                )


async def test_the_password_never_reaches_the_journal(service_client, practice):
    """Оговорка к предыдущему: запрещено не только ИМЯ, но и само значение.

    Проверка имён поймала бы поле `password`, но не поймала бы пароль, попавший
    в чужое поле или в текст. Здесь пароль настоящий и заведомо был у процесса
    в руках — если он всплывёт где угодно в записи, это видно.
    """
    async with service_client.capture_logs() as capture:
        assert (await sign_in(service_client)).status == 200

    for record in capture.select():
        for field, value in record.items():
            assert PASSWORD not in str(value), f'пароль оказался в поле «{field}»'
            assert EMAIL not in str(value), f'почта оказалась в поле «{field}»'
