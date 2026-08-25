-- Реестр применённых миграций.
--
-- Это метатаблица механизма, а не предметная область: tenant_id здесь нет
-- намеренно, и это исключение записано явным списком в
-- scripts/check_migrations.py, а не оставлено на усмотрение читающего.
--
-- Заводится первой миграцией, потому что механизм применения должен уметь
-- записать сам факт применения — включая своё собственное.

create table schema_version (
    version    integer     primary key,
    applied_at timestamptz not null default now(),
    checksum   char(64)    not null
);

comment on table schema_version is 'Применённые миграции: версия, момент применения в UTC и контрольная сумма файла.';
