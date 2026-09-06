"""УЧАСТИЕ СКВОЗЬ ВСЕ СЛОИ: ручка, сценарий, домен, база.

Групповые занятия заложены в модель и не введены (`PDR-SCHED-08`), поэтому
главная проверка здесь — РЕГРЕСС: занятие один-на-один ведёт себя ровно как
раньше. Это и есть основной риск задачи, и проверяется он не «мы посмотрели», а
теми же обменами, что и до неё.

Второго участника через ручку не назвать вовсе: в схеме запроса один `student`
(docs/api/openapi.yaml), и второго поля там нет. Что домен отклоняет группу
понятной ошибкой, проверяет unit-прогон — снаружи до этого правила не
дотянуться, и это тоже часть решения: «ни одного экрана групповых занятий».
"""

import uuid

from contour import CABINET
from contour import MOSCOW
from contour import STUDENT
from contour import TUTOR
from contour import in_a_week
from contour import sign_in

HOUR = 3600000000
DAY = 24 * HOUR

MARCH = in_a_week()


def key():
    return f'participation-{uuid.uuid4()}'


async def book(service_client, headers, starts_at=MARCH):
    return await service_client.post(
        '/api/v1/lessons',
        json={
            'tutor': TUTOR,
            'student': STUDENT,
            'starts_at': starts_at,
            'minutes': 60,
            'tz': MOSCOW,
        },
        headers={**headers, 'Idempotency-Key': key()},
    )


def participations(pgsql):
    rows = pgsql['pdr'].cursor()
    rows.execute(
        'select participant_id::text, price_minor, currency, payment, attendance, state '
        'from scheduling_lesson_participant where tenant_id = %s '
        'order by participant_id',
        (CABINET,),
    )
    return rows.fetchall()


async def test_a_one_to_one_lesson_behaves_exactly_as_before(service_client, cabinet):
    """РЕГРЕСС: запись, ответ и выдача не изменились ни в одном поле.

    Участники в ответе — список идентификаторов, как и были: модель под ними
    поменялась, контракт наружу — нет.
    """
    tutor = await sign_in(service_client, TUTOR)

    created = await book(service_client, tutor)

    assert created.status == 200, created.text
    lesson = created.json()
    assert lesson['participants'] == [STUDENT]
    assert lesson['tutor'] == TUTOR
    assert lesson['state'] == 'planned'
    assert lesson['ends_at'] == MARCH + HOUR

    found = await service_client.get(
        '/api/v1/lessons',
        params={'from': MARCH - DAY, 'to': MARCH + DAY, 'side': 'tutor'},
        headers=tutor,
    )
    assert found.status == 200, found.text
    assert found.json()['lessons'] == [lesson]


async def test_booking_writes_one_participation_with_its_own_columns(
    service_client, cabinet, pgsql,
):
    """Занятие записано — участие лежит строкой, и у него СВОИ поля.

    Цена пустая: назначает её не расписание, и на момент записи её ещё нет.
    Пустая цена не значит «бесплатно» — это проверяет и схема, и домен.
    """
    tutor = await sign_in(service_client, TUTOR)
    assert (await book(service_client, tutor)).status == 200

    rows = participations(pgsql)

    assert len(rows) == 1, 'участие завелось не одно'
    participant, price_minor, currency, payment, attendance, state = rows[0]
    assert participant == STUDENT
    assert price_minor is None, 'цена завелась сама'
    assert currency is None
    assert payment == 'unpaid'
    assert attendance == 'expected'
    assert state == 'joined'


async def test_the_answer_names_participants_and_not_a_group(service_client, cabinet):
    """НИ ОДНОГО ЭКРАНА ГРУПП: в ответе нет ни группы, ни размера, ни второго
    места — только те же участники, что и раньше."""
    tutor = await sign_in(service_client, TUTOR)

    lesson = (await book(service_client, tutor)).json()

    assert set(lesson) == {
        'id',
        'tutor',
        'participants',
        'starts_at',
        'ends_at',
        'tz',
        'state',
    }, 'в ответе занятия появилось поле, которого не было'
