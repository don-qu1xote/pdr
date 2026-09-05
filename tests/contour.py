"""Общая оснастка контура: одна на все наборы, которые поднимают процесс.

Наборов уже два — сценарии сервиса и сверка с контрактом, — и оба поднимают один
и тот же процесс на одной и той же схеме. Вторая копия этих фикстур разошлась бы
с первой в тот день, когда правку внесли в одну, и разошлась бы молча: оба набора
остались бы зелёными, каждый на своём представлении о базе.

Сервис поднимает и гасит ПЛАГИН, а не этот файл (ADR-0012). Здесь только то,
чего плагин знать не может: где лежит схема, как называется компонент базы и
какие пути в установке указывают наружу процесса.
"""

import datetime
import pathlib
import sys
import uuid

import pytest
from testsuite.databases.pgsql import discover

USERVER_CONFIG_HOOKS = ['pdr_config_paths']

ROOT = pathlib.Path(__file__).resolve().parents[1]
MIGRATIONS = ROOT / 'db' / 'migrations'
OPENAPI = ROOT / 'docs' / 'api' / 'openapi.yaml'

sys.path.insert(0, str(ROOT / 'scripts'))
import migration_model  # noqa: E402  (после правки sys.path)

DATABASE = 'pdr'

SYSTEM_TENANT = '00000000-0000-4000-8000-000000000001'
"""Тот же идентификатор, что в db/migrations/V012__system_tenant.sql и в
jobs::SystemTenant(). Третьей записи значения нет: она разошлась бы."""

POSTGRES_COMPONENT = 'postgres-pdr'

CABINET = '11111111-1111-4111-8111-111111111111'
PARTICIPANT = '22222222-2222-4222-8222-222222222222'
CABINET_EMAIL = 'nina@example.org'
CABINET_PASSWORD = 'correct-horse-battery'

CABINET_PASSWORD_HASH = (
    '$argon2id$v=19$m=65536,t=3,p=1$cGRyLWNvbnRyYWN0LXNsdA'
    '$XGIFHfMENSnGu6lfQYEW/Ikhm2vP48/FF5k9G+yAnmg'
)
"""Argon2id от CABINET_PASSWORD, посчитанный один раз и записанный сюда.

Параметры счёта лежат ВНУТРИ записи, поэтому проверка не зависит ни от текущего
PDR_SIGN_IN_RULES, ни от того, чем хеш посчитали. Считать его в прогоне значило
бы тратить на каждый тест десятки миллисекунд ровно ради того, что и так
проверено contract-набором счёта паролей.
"""


@pytest.fixture(scope='session')
def pgsql_local(pgsql_local_create):
    """База контура: все миграции подряд, в порядке версий.

    Схема берётся из НАСТОЯЩИХ db/migrations, а не из отдельного файла для
    тестов: вторая копия схемы расходится с первой в тот день, когда правку
    внесли в одну, и прогон остаётся зелёным на схеме, которой в установке нет.
    """
    files = sorted(MIGRATIONS.glob('V*.sql'))
    assert files, f'миграций нет в {MIGRATIONS}'

    shard = discover.PgShard(
        shard_id=discover.SINGLE_SHARD,
        pretty_name=DATABASE,
        dbname=DATABASE,
        files=files,
        migrations=[],
    )
    return pgsql_local_create([
        discover.PgShardedDatabase(
            service_name=None,
            dbname=DATABASE,
            shards=[shard],
        ),
    ])


@pytest.fixture(autouse=True)
def applied_migrations(pgsql):
    """Реестр применённых миграций заполняется, как это делает установка.

    Схему контуру накатывает плагин — просто прогоняя файлы, — и записи в
    `schema_version` после него нет. А готовность процесса спрашивает именно
    её: схема, отставшая на миграцию, страшнее недоступной базы, потому что
    сервис на ней отвечает и молча делает не то.

    Поэтому реестр заполняется теми же версиями и теми же суммами, что записал
    бы `scripts/migrate.py`: считает их та же функция, а не вторая её копия.
    Иначе контур проверял бы готовность на реестре, которого в установке не
    бывает.

    Заодно возвращается арендатор системы: он часть СХЕМЫ, а не данных теста —
    его заводит миграция V012, а плагин чистит таблицы перед каждым тестом и
    уносит его вместе с ними. Без него кластерное задание не поставит след:
    внешний ключ `jobs_effect` ведёт сюда
    (docs/adr/0021-system-tenant-for-clusterwide-jobs.md).
    """
    rows = pgsql[DATABASE].cursor()
    for migration in migration_model.load(MIGRATIONS):
        rows.execute(
            'insert into schema_version (version, checksum) values (%s, %s) '
            'on conflict (version) do nothing',
            (migration.version, migration.checksum),
        )

    rows.execute(
        "insert into identity_tenant (tenant_id, name, tz) "
        "values (%s, 'Система', 'UTC') on conflict (tenant_id) do nothing",
        (SYSTEM_TENANT,),
    )


@pytest.fixture(scope='session')
def userver_pg_config(pgsql_local):
    """Адрес базы — компоненту по имени.

    Штатный хук ищет компонент по наличию `dbconnection` в статическом конфиге, а
    у нас там `dbalias`: строка подключения приходит из файла секретов, а не из
    конфига. Поэтому компонент называется прямо — плагин это и предлагает
    сделать переопределением.
    """
    uri = pgsql_local[DATABASE].get_uri()

    def patch(config_yaml, config_vars):
        component = config_yaml['components_manager']['components'][POSTGRES_COMPONENT]
        component['dbconnection'] = uri
        component.pop('dbalias', None)

    return patch


@pytest.fixture(scope='session')
def pdr_config_paths(tmp_path_factory):
    """Пути и переключатели, которые в установке указывают наружу процесса.

    Файл значений один на все профили: разница между установкой и контуром — не
    второй конфиг, а другие значения переменных. Здесь их подставляет контур,
    как штатные хуки подставляют адрес базы и путь к секретам.

    Доставка динамических значений включается ЗДЕСЬ, а не в установке: сервера
    конфигов в установке нет, а контур поднимает свой (mock_configs_service).
    Иначе «значение меняется без перезапуска» осталось бы непроверенным до дня,
    когда сервер появится.
    """
    dumps = tmp_path_factory.mktemp('dumps')

    def patch(config_yaml, config_vars):
        config_vars['openapi-document'] = str(OPENAPI)
        config_vars['dump-root'] = str(dumps)
        config_vars['dynamic-config-updates'] = True

    return patch


TUTOR = '33333333-3333-4333-8333-333333333333'
STUDENT = '44444444-4444-4444-8444-444444444444'
GUARDIAN = '55555555-5555-4555-8555-555555555555'
OUTSIDER = '66666666-6666-4666-8666-666666666666'

MOSCOW = 'Europe/Moscow'


def mail(person):
    return f'{person[:8]}@example.org'


class Cabinet:
    """Кто заведён в кабинете расписания. Одна засыпка на оба набора.

    Людей четверо, и меньшим составом не обойтись: «ученик не видит чужие
    занятия» требует второго ученика, а «опекун без доступа получает отказ» —
    опекуна, у которого опека есть, а уровня нет.
    """

    def __init__(self, rows):
        self.rows = rows

    def lessons(self):
        self.rows.execute('select count(*) from scheduling_lesson')
        return self.rows.fetchone()[0]

    def open_schedule_to_the_guardian(self):
        """Уровень доступа открывают ОТДЕЛЬНО от опеки — строкой согласия.

        Согласие дано ВЧЕРА, а не сейчас: часы базы и часы процесса в контуре
        расходятся на смещение зоны, и согласие, выданное «сию секунду»,
        оказывается для процесса будущим — а будущее согласие не действует.
        Вчерашнее и ближе к жизни: доступ открывают заранее, а не в тот же миг.
        """
        self.rows.execute(
            'insert into identity_guardian_consent '
            '(tenant_id, id, guardian_id, student_id, scope, granted_by, granted_at) '
            "values (%s, %s, %s, %s, 'schedule', %s, now() - interval '1 day')",
            (CABINET, str(uuid.uuid4()), GUARDIAN, STUDENT, TUTOR),
        )
        self.rows.connection.commit()


def _enrol(rows, person, role):
    rows.execute(
        'insert into identity_person (tenant_id, id, display_name, email, tz) '
        "values (%s, %s, %s, %s, 'Europe/Moscow') on conflict do nothing",
        (CABINET, person, role, mail(person)),
    )
    rows.execute(
        'insert into identity_credential (tenant_id, person_id, password_hash) '
        'values (%s, %s, %s) on conflict do nothing',
        (CABINET, person, CABINET_PASSWORD_HASH),
    )
    rows.execute(
        'insert into identity_role_assignment (tenant_id, id, person_id, role) '
        'values (%s, %s, %s, %s)',
        (CABINET, str(uuid.uuid4()), person, role),
    )


@pytest.fixture
def cabinet(practice):
    """Кабинет расписания: репетитор, его ученик, опекун и посторонний ученик."""
    _enrol(practice, TUTOR, 'tutor')
    _enrol(practice, STUDENT, 'student')
    _enrol(practice, GUARDIAN, 'guardian')
    _enrol(practice, OUTSIDER, 'student')
    practice.execute(
        'insert into identity_guardianship (tenant_id, id, guardian_id, student_id) '
        'values (%s, %s, %s, %s)',
        (CABINET, str(uuid.uuid4()), GUARDIAN, STUDENT),
    )
    return Cabinet(practice)


async def sign_in(service_client, person):
    """Сессия человека — тем же путём, каким её получает клиент."""
    response = await service_client.post(
        f'/api/v1/cabinet/{CABINET}/sign-in',
        json={'email': mail(person), 'password': CABINET_PASSWORD},
        headers={'Idempotency-Key': f'sign-in-{uuid.uuid4()}'},
    )
    assert response.status == 200, response.text

    return {'Cookie': response.headers['Set-Cookie'].split(';', maxsplit=1)[0]}


def in_a_week():
    """Момент через неделю, ровно в час.

    Считается от текущего времени, а не записан константой: занятие в прошлом
    домен не принимает, и записанная дата превратила бы набор в бомбу с часовым
    механизмом — зелёный сегодня и красный в тот день, когда она пройдёт.
    """
    at = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=7)
    return int(at.replace(minute=0, second=0, microsecond=0).timestamp()) * 1000000


def next_tuesday():
    """Ближайший вторник после сегодняшнего — по той же причине."""
    today = datetime.datetime.now(datetime.timezone.utc).date()
    return (today + datetime.timedelta(days=(1 - today.weekday()) % 7 or 7)).isoformat()


@pytest.fixture
def practice(pgsql):
    """Кабинет, в который можно войти: практика, человек и его пароль.

    Одна на оба набора. Две копии этой засыпки разошлись бы молча — и каждый
    набор остался бы зелёным на своём представлении о том, кто заведён.
    """
    rows = pgsql[DATABASE].cursor()
    rows.execute(
        "insert into identity_tenant (tenant_id, name, tz) "
        "values (%s, 'Нина', 'Europe/Moscow') on conflict do nothing",
        (CABINET,),
    )
    rows.execute(
        'insert into identity_person (tenant_id, id, display_name, email, tz) '
        "values (%s, %s, 'Нина', %s, 'Europe/Moscow') on conflict do nothing",
        (CABINET, PARTICIPANT, CABINET_EMAIL),
    )
    rows.execute(
        'insert into identity_credential (tenant_id, person_id, password_hash) '
        'values (%s, %s, %s) on conflict do nothing',
        (CABINET, PARTICIPANT, CABINET_PASSWORD_HASH),
    )
    return rows
