-- Журнал доступа к чувствительным данным.
--
-- Право смотреть — не то же самое, что право смотреть незаметно
-- (docs/adr/0006-parental-access-by-design.md). Кто и когда открывал запись
-- занятия, транскрипт или переписку — строка здесь, и она видна и ученику, и
-- репетитору.
--
-- Таблица заводится сейчас, а не вместе с родительским доступом: журнал,
-- прошитый через готовый код задним числом, пропускает ровно те места, где
-- смотрели молча. Пишет в неё точка записи из
-- libs/pdr-identity/src/identity/infrastructure/audit/.
--
-- Владение таблицами — docs/architecture/context-map.md (контекст identity).

create table identity_access_log (
    tenant_id     uuid        not null references identity_tenant (tenant_id),
    id            uuid        not null,
    actor_id      uuid        not null,
    subject_id    uuid        not null,
    resource_kind text        not null,
    at            timestamptz not null default now(),
    constraint identity_access_log_pk primary key (tenant_id, id),
    constraint identity_access_log_actor_fk
        foreign key (tenant_id, actor_id) references identity_person (tenant_id, id),
    constraint identity_access_log_subject_fk
        foreign key (tenant_id, subject_id) references identity_person (tenant_id, id),
    -- Своё чтение не журналируется: незаметно смотрят чужое, а не собственное.
    -- Тот же отказ стоит в домене — AccessRecord::Of.
    constraint identity_access_log_not_self check (actor_id <> subject_id),
    -- Список видов закрыт и повторяет ADR-0006: содержание занятия, а не всё
    -- подряд. Журнал обо всём — это журнал, который никто не читает.
    constraint identity_access_log_kind_known
        check (resource_kind in ('recording', 'transcript', 'chat'))
);
comment on table identity_access_log is 'Кто и когда смотрел запись занятия, транскрипт или переписку. Право смотреть не то же самое, что право смотреть незаметно.';

-- Главный вопрос к журналу: «кто смотрел мои данные». Отсюда порядок колонок.
create index identity_access_log_by_subject
    on identity_access_log (tenant_id, subject_id, at desc);

alter table identity_access_log enable row level security;
alter table identity_access_log force row level security;

create policy identity_access_log_isolation on identity_access_log
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. update и delete не выдаются: строка журнала не
-- правится и не исчезает — иначе на вопрос «кто смотрел в марте» отвечает не
-- журнал, а тот, у кого была причина его подчистить.
grant select, insert on identity_access_log to pdr_app;
