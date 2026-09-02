"""Общая оснастка контура: одна на все наборы, которые поднимают процесс.

Наборов уже два — сценарии сервиса и сверка с контрактом, — и оба поднимают один
и тот же процесс на одной и той же схеме. Вторая копия этих фикстур разошлась бы
с первой в тот день, когда правку внесли в одну, и разошлась бы молча: оба набора
остались бы зелёными, каждый на своём представлении о базе.

Сервис поднимает и гасит ПЛАГИН, а не этот файл (ADR-0012). Здесь только то,
чего плагин знать не может: где лежит схема, как называется компонент базы и
какие пути в установке указывают наружу процесса.
"""

import pathlib
import sys

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
