-- Исходящая очередь оповещений: транзакционный outbox.
--
-- Строка кладётся сюда В ТОЙ ЖЕ ТРАНЗАКЦИИ, что и изменение, о котором она
-- рассказывает. В этом весь смысл: отправить письмо и упасть до коммита занятия
-- нельзя, потому что письма ещё нет; записать занятие и упасть до письма —
-- тоже, потому что строка уже лежит и её заберут. Отправка отдельным вызовом из
-- обработчика запроса даёт ровно обе эти дырки, и обходятся они не аккуратностью,
-- а транзакцией (docs/adr/0002-postgres-only-storage.md).
--
-- Очередь — таблица, а не брокер. Второе хранилище пришлось бы держать живым,
-- обновлять и объяснять, а `select ... for update skip locked` уже есть в базе,
-- которая и так стоит.
--
-- Разбирает очередь ФОНОВЫЙ ОТПРАВЩИК, у которого нет ни запроса, ни человека,
-- ни практики: он спрашивает «чему настал срок», а не «что у арендатора X».
-- Поэтому у таблицы вторая политика и отдельное объявление
-- (docs/adr/0022-outbox-dispatch-across-tenants.md). Изоляция при этом не
-- выключается: без объявления разбора не видно по-прежнему ничего.
--
-- Владение таблицей — docs/architecture/context-map.md, контекст notifications.

create table notifications_outbox (
    tenant_id       uuid        not null references identity_tenant (tenant_id),
    id              uuid        not null,
    -- Имя доменного события: `scheduling.lesson_booked` и подобные. Имя, а не
    -- текст письма: слова подставляет шаблон при отправке, иначе то, что видит
    -- человек, зашито в бэкенде и не меняется без выкатки.
    event_type      text        not null,
    -- Нагрузка события. jsonb, а не text: по ней ищут и её читают глазами при
    -- разборе жалобы, а `jsonb` показывает форму, которой text не показывает.
    payload         jsonb       not null,
    -- КЛЮЧ ОДНОГО НАМЕРЕНИЯ. Доставка «не менее одного раза» означает, что одно
    -- и то же событие придёт получателю дважды, если сеть моргнула в неудачный
    -- момент; ключ — это то, по чему получатель (и мы сами при вставке) узнаёт
    -- повтор. Он детерминирован: «напомнить о занятии 7f3c… за час» — один и тот
    -- же ключ у любого процесса и в любом прогоне.
    dedup_key       text        not null,
    created_at      timestamptz not null,
    state           text        not null default 'pending',
    attempts        integer     not null default 0,
    -- КОГДА ПРОБОВАТЬ. Отсюда же берутся напоминания: строка с моментом в
    -- будущем — это и есть отложенное оповещение, и никакого крона, перебирающего
    -- все занятия, для него не нужно.
    next_attempt_at timestamptz not null,
    failed_reason   text        not null default '',
    constraint notifications_outbox_pk primary key (tenant_id, id),
    -- Одно намерение — одна строка. Повторный запрос человека (тот же ключ
    -- повтора), второй подписчик, перезапуск сценария — всё это приходит сюда
    -- одной строкой, а не тремя письмами.
    constraint notifications_outbox_once unique (tenant_id, dedup_key),
    constraint notifications_outbox_state_known
        check (state in ('pending', 'sent', 'gave_up')),
    constraint notifications_outbox_attempts_not_negative check (attempts >= 0),
    -- Причина есть ровно тогда, когда сдались. Пустая причина у сдавшейся строки
    -- — это «почему-то не ушло», и разбирать такое некому; причина у ушедшей —
    -- след прошлой неудачи, который читается как новая.
    constraint notifications_outbox_reason_only_when_gave_up
        check ((state = 'gave_up') = (length(failed_reason) > 0)),
    constraint notifications_outbox_dedup_not_blank check (length(btrim(dedup_key)) > 0),
    constraint notifications_outbox_type_not_blank check (length(btrim(event_type)) > 0),
    constraint notifications_outbox_payload_is_object check (jsonb_typeof(payload) = 'object')
);
comment on table notifications_outbox is 'Очередь на отправку: строка кладётся в той же транзакции, что и изменение, о котором она рассказывает, а разбирается через select ... for update skip locked.';
comment on column notifications_outbox.dedup_key is 'Ключ одного намерения: по нему повтор узнаётся и при вставке, и получателем. Детерминированный, без «сейчас» внутри.';
comment on column notifications_outbox.next_attempt_at is 'Когда пробовать. Момент в будущем — это отложенное оповещение: напоминание, а не крон по занятиям.';

-- ИНДЕКС ПОД ЗАПРОС ОТПРАВЩИКА, и он нарочно НЕ начинается с арендатора.
--
-- Отправщик спрашивает «чему настал срок» сразу у всех: у него нет практики, от
-- имени которой он работал бы. Индекс, начинающийся с `tenant_id`, такому
-- запросу не помогает вовсе — пришлось бы читать его целиком. Частичный: строки
-- ушедшие и сдавшиеся в очереди больше не участвуют, и держать их в индексе
-- значит платить за них на каждой вставке.
--
-- Прецедент тот же: `observability_product_event_by_age` по `recorded_at`.
create index notifications_outbox_due on notifications_outbox (next_attempt_at)
    where state = 'pending';

-- Удаление — для отзыва. Занятие отменили, а напоминание «завтра в 17:00» лежит
-- и ждёт своего часа: отправить его после отмены значит соврать человеку.
-- Отозванная строка удаляется, а не помечается: состояний у неё ровно три, и
-- «отозвано» среди них нет — заводить четвёртое ради этого значит менять смысл
-- колонки, о котором договорились.
grant select, insert, update, delete on notifications_outbox to pdr_app;

alter table notifications_outbox enable row level security;
alter table notifications_outbox force row level security;
create policy notifications_outbox_isolation on notifications_outbox
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- ВТОРАЯ ДВЕРЬ, И ОНА ОДНА НА ВСЮ СХЕМУ.
--
-- Отправщик — фоновая работа без арендатора по существу. Списка практик ему
-- взять неоткуда: `identity_tenant` лежит под той же политикой, что и всё
-- остальное, и обойти её нечем. Обход «по кругу, по одной практике» — это и есть
-- перебор, которого очередь избегает.
--
-- Параметр ОТДЕЛЬНЫЙ, а не значение `pdr.tenant_id`. Забытый арендатор
-- по-прежнему означает пустой ответ, а не чужие строки: чтобы увидеть очередь
-- целиком, надо объявить разбор явно, и объявляет его один запрос
-- (db/sql/notifications/notifications_declare_dispatch.sql) на время одной
-- транзакции. Список таблиц с такой политикой закрыт в scripts/check_rls.py.
create policy notifications_outbox_dispatch on notifications_outbox
    using (current_setting('pdr.dispatch', true) = 'on')
    with check (current_setting('pdr.dispatch', true) = 'on');
