-- Поток продуктовых событий: отдельная таблица, отдельный срок жизни.
--
-- Продуктовое событие отвечает на вопрос «что сделал человек», а не «сколько
-- миллисекунд занял запрос». Технические метрики живут ШТАТНЫМИ средствами
-- userver (utils::statistics) и в базу не попадают вовсе: у двух потоков разные
-- читатели, разные права и разный срок жизни. Смешать их в одной таблице —
-- значит выбрать один срок для обоих и ошибиться дважды.
--
-- Зачем поток вообще нужен и какие вопросы он закрывает —
-- docs/product/open-questions.md и docs/architecture/product-events.md.
--
-- Владение таблицами — docs/architecture/context-map.md (контекст observability).
-- Правила колонок — docs/architecture/migrations.md.

-- Одна запись — одно продуктовое событие.
--
-- ОБЕЗЛИЧИВАНИЕ ЗДЕСЬ СТРУКТУРНОЕ, А НЕ ПО ДИСЦИПЛИНЕ. Колонки человека нет, и
-- завести её нельзя, не тронув эту миграцию; а спрятать идентификатор внутрь
-- fields не даёт ограничение observability_product_event_fields_are_anonymous:
-- ключ, похожий на ссылку на человека, отвергается базой. Не «договорились не
-- писать», а «такая строка не вставляется».
--
-- Ссылка на арендатора и роль остаётся: вопрос «репетиторы правят конспекты?»
-- требует знать роль, но не требует знать, кто именно правил.
--
-- Зоны IANA рядом со временем здесь нет намеренно, хотя в остальной схеме она
-- есть: продуктовое событие — это момент, а не задуманное человеком время.
-- «В среду в 17:00» бывает у занятия, а не у факта «занятие отменено».
--
-- Два времени, и это не избыточность:
--
--   occurred_at  когда произошло — по часам издателя, то есть по порту Clock;
--   recorded_at  когда попало в таблицу.
--
-- Разделение нужно ради срока жизни: уборка идёт по recorded_at, потому что
-- событие с подделанным или уехавшим occurred_at не должно ни выживать вечно,
-- ни исчезать раньше остальных.
create table observability_product_event (
    tenant_id   uuid        not null references identity_tenant (tenant_id),
    id          uuid        not null,
    type        text        not null,
    version     integer     not null,
    actor_role  text        not null,
    occurred_at timestamptz not null,
    recorded_at timestamptz not null default now(),
    fields      jsonb       not null,
    constraint observability_product_event_pk primary key (tenant_id, id),
    -- Имя типа — «контекст.что_произошло»: по нему видно издателя.
    constraint observability_product_event_type_shaped
        check (type ~ '^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$'),
    -- Схема без версии запрещена: менять её придётся, и не один раз. Версия
    -- лежит в самой записи, потому что читать её будут после смены схемы.
    constraint observability_product_event_version_from_one check (version >= 1),
    constraint observability_product_event_role_known
        check (actor_role in ('tutor', 'student', 'guardian', 'system')),
    constraint observability_product_event_fields_are_object
        check (jsonb_typeof(fields) = 'object'),
    -- ГЛАВНОЕ ОГРАНИЧЕНИЕ ЭТОЙ ТАБЛИЦЫ. Ключ, именующий человека, не вставляется:
    -- ни «*_id», ни почта, ни телефон, ни имя. Тот же список проверяет
    -- scripts/check_product_events.py на реестре событий и домен при сборке
    -- записи — правило одно, застав его нарушение, отказывают все трое.
    constraint observability_product_event_fields_are_anonymous
        check (not jsonb_path_exists(
            fields,
            '$.keyvalue() ? (@.key like_regex "_id|^id|person|email|phone|login|name")'
        )),
    constraint observability_product_event_recorded_after_occurred
        check (recorded_at >= occurred_at)
);
comment on table observability_product_event is 'Поток продуктовых событий: что сделал человек, обезличенно. Ссылка на арендатора и роль, идентификатора человека нет ни колонкой, ни ключом в fields. Отдельно от технических метрик: у них разные читатели, права и срок жизни.';

-- Уборка идёт по возрасту записи: без индекса она превращается в перебор всей
-- таблицы, а таблица потока — самая быстрорастущая из всех.
create index observability_product_event_by_age on observability_product_event (recorded_at);

-- Чтение под вопрос: «покажи все оценки за последние тридцать дней». Тип первым
-- после арендатора, потому что вопрос всегда про один тип события.
create index observability_product_event_by_type
    on observability_product_event (tenant_id, type, occurred_at);

-- ИЗОЛЯЦИЯ. Правило общее и исключений для потока не делает: продуктовое
-- событие принадлежит арендатору так же, как занятие.
--
-- Роль приложения без объявленного арендатора не увидит ни строки — в том числе
-- при выгрузке. Читать поток целиком может только тот, кто и так может всё:
-- администратор кластера. Именно поэтому в потоке нет идентификатора человека —
-- обезличивание не «на случай утечки», а условие, при котором такое чтение
-- вообще допустимо (docs/architecture/product-events.md).
alter table observability_product_event enable row level security;
alter table observability_product_event force row level security;

create policy observability_product_event_isolation on observability_product_event
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. update нет: записанное событие не переписывается —
-- иначе «что было на самом деле» становится вопросом аккуратности запроса.
-- delete есть ровно для уборки по сроку жизни, и политика выше не даёт ей
-- вычистить чужое.
grant select, insert, delete on observability_product_event to pdr_app;
