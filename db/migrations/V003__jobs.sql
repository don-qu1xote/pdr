-- Одиночные фоновые задания: блокировка, журнал запусков, следы действий.
--
-- Очередь на SKIP LOCKED решает другую задачу — «много воркеров разбирают много
-- работ». Здесь третья таблица про третье: «ровно один воркер во всём кластере
-- делает периодическую работу» — рассылку напоминаний, пересчёт горизонта,
-- сверку платежей.
--
-- Блокировку берёт ШТАТНЫЙ storages::postgres::DistLock (userver), поэтому
-- форма jobs_lock задана им, а не нами: три колонки и уникальный key. Своей
-- блокировки через «update ... set locked = true» здесь нет и быть не может —
-- она ломается ровно на перезапуске: строка остаётся занятой умершим воркером,
-- и снять её некому. Решение и цена — docs/adr/0011-single-jobs-on-distlock.md.
--
-- Владение таблицами — docs/architecture/context-map.md (контекст jobs).
-- Правила колонок — docs/architecture/migrations.md.

-- Блокировка. Одна строка на задание во всём кластере; арендатора у неё нет и
-- быть не может — задание одно на всех, а не по одному на каждого.
--
-- Колонки диктует запрос userver (INSERT ... ON CONFLICT (key) DO UPDATE ...
-- WHERE t.owner = $2 OR t.expiration_time <= current_timestamp), поэтому имена
-- именно такие. Срок живёт в базе, а не в памяти воркера: умерший воркер
-- перестаёт продлевать строку, и через expiration_time её забирает живой.
create table jobs_lock (
    key             text        not null,
    owner           text,
    expiration_time timestamptz,
    constraint jobs_lock_pk primary key (key),
    constraint jobs_lock_key_not_blank check (length(btrim(key)) > 0)
);
comment on table jobs_lock is 'Распределённая блокировка периодических заданий: одна строка на задание во всём кластере. Форму таблицы задаёт штатный storages::postgres::DistLock.';

-- Журнал запусков: когда последний раз отработало и сколько заняло.
--
-- Одна строка на задание, а не история: вопрос, на который она отвечает, ровно
-- один — «задание ещё живо?». Возраст последней записи и есть метрика
-- (docs/architecture/jobs.md): растёт — значит воркер стоит, и это видно, а не
-- выясняется по жалобе.
--
-- Три момента времени, и это не избыточность:
--
--   attempt_at              начало последней ПОПЫТКИ, в том числе идущей сейчас;
--   started_at, finished_at начало и конец последнего ЗАВЕРШЁННОГО прогона.
--
-- Разделение нужно ровно затем, чтобы во время прогона возраст последнего
-- УДАЧНОГО остался известен: воркер, начавший работу и упавший, обязан
-- выглядеть замолчавшим, а не отработавшим только что.
--
-- Арендатора нет по той же причине, что у блокировки: прогон один на кластер.
-- Счётчики действий — за последний завершённый прогон, runs — за всю жизнь
-- задания.
create table jobs_run (
    job         text        not null,
    attempt_at  timestamptz not null,
    started_at  timestamptz,
    finished_at timestamptz,
    duration_ms bigint,
    outcome     text        not null,
    produced    bigint      not null default 0,
    repeated    bigint      not null default 0,
    runs        bigint      not null default 0,
    constraint jobs_run_pk primary key (job),
    constraint jobs_run_job_not_blank check (length(btrim(job)) > 0),
    constraint jobs_run_outcome_known
        check (outcome in ('running', 'done', 'lock_lost', 'timed_out')),
    -- Завершённый прогон описан целиком или не описан вовсе: половина записи
    -- («конец есть, длительности нет») превратилась бы в пустое место в метрике.
    constraint jobs_run_finished_is_whole
        check ((finished_at is null) = (started_at is null)
               and (finished_at is null) = (duration_ms is null)),
    constraint jobs_run_duration_not_negative check (duration_ms is null or duration_ms >= 0),
    constraint jobs_run_finished_after_started
        check (finished_at is null or finished_at >= started_at),
    constraint jobs_run_counters_not_negative
        check (produced >= 0 and repeated >= 0 and runs >= 0)
);
comment on table jobs_run is 'Последний прогон задания: начало попытки, начало и конец последнего завершённого прогона, длительность и исход. Возраст этой записи — метрика живости задания.';

-- След произведённого действия — то, из чего сделана идемпотентность.
--
-- Ключ действия детерминированный: одна и та же работа даёт один и тот же
-- effect_key у любого воркера и в любом прогоне. Первичный ключ и есть
-- проверка: второй воркер, подхвативший работу после потери блокировки,
-- получает на вставке конфликт, действие не производит и идёт дальше. Не
-- «договорились не отправлять дважды», а «вторая отправка не выражается в
-- схеме».
--
-- Арендатор здесь есть: действие всегда чьё-то. Фоновая работа ходит под
-- арендатором так же, как запрос человека, — политика ниже не делает для неё
-- исключения.
create table jobs_effect (
    tenant_id   uuid        not null references identity_tenant (tenant_id),
    job         text        not null,
    effect_key  text        not null,
    produced_at timestamptz not null default now(),
    constraint jobs_effect_pk primary key (tenant_id, job, effect_key),
    constraint jobs_effect_job_not_blank check (length(btrim(job)) > 0),
    constraint jobs_effect_key_not_blank check (length(btrim(effect_key)) > 0)
);
comment on table jobs_effect is 'След уже произведённого действия задания: (арендатор, задание, ключ действия). Повтор после потери блокировки упирается в первичный ключ и второго действия не производит.';

-- Уборка следов идёт по времени: старые ключи никому не нужны, а таблица без
-- этого растёт вечно.
create index jobs_effect_by_age on jobs_effect (produced_at);

-- ИЗОЛЯЦИЯ.
--
-- jobs_lock и jobs_run — таблицы механизма: арендатора у них нет, потому что
-- одиночное задание принадлежит кластеру, а не арендатору. Они перечислены
-- поимённо в scripts/migration_model.py и там же объяснены.
--
-- jobs_effect — с арендатором, и правило для неё общее. Метатаблица с
-- tenant_id исключением НЕ считается: это проверяет scripts/check_rls.py.
alter table jobs_effect enable row level security;
alter table jobs_effect force row level security;

create policy jobs_effect_isolation on jobs_effect
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. Поимённо и по делу. У jobs_effect нет update: уже
-- произведённое действие не переписывается, иначе идемпотентность становится
-- вопросом аккуратности запроса. delete есть ровно для уборки старых следов —
-- она такое же одиночное задание, и политика выше не даёт ей вычистить чужое.
grant select, insert, update, delete on jobs_lock to pdr_app;
grant select, insert, update on jobs_run to pdr_app;
grant select, insert, delete on jobs_effect to pdr_app;
