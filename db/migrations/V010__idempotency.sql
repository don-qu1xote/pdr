-- Идемпотентность мутирующих запросов: ключ, отпечаток тела и сохранённый ответ.
--
-- ЗАВЕДЕНО ДО БИЛЛИНГА НАМЕРЕННО. Если вводить идемпотентность вместе с
-- деньгами, первый же баг стоит двойного списания у живого человека: связь
-- оборвалась, клиент повторил, и второй платёж прошёл. Здесь связь обрывается
-- ровно так же, но повтор упирается в первичный ключ.
--
-- Три таблицы про идемпотентность в дереве уже есть, и они про разное:
--
--   jobs_effect              «это действие уже произведено» внутри одного
--                            периодического задания, ключ придумывает задание;
--   http_idempotency_key     «этот запрос уже обслужен», ключ придумывает
--                            КЛИЕНТ и присылает заголовком;
--   очередь на SKIP LOCKED   «эту работу уже разбирает другой воркер».
--
-- Владение таблицами — docs/architecture/context-map.md (контекст http).
-- Правила колонок — docs/architecture/migrations.md.
-- Поведение целиком — docs/architecture/idempotency.md.

-- Ключ клиента и всё, что нужно, чтобы ответить на повтор тем же самым.
--
-- Первичный ключ — (tenant_id, key). Не только key: ключи придумывает клиент,
-- и «idempotency-1» у двух разных практик встретятся в первый же день. Пара с
-- арендатором делает их разными, а построчная защита не даёт узнать о чужом.
--
-- request_fingerprint — ОТПЕЧАТОК тела, а не тело. Тело хранить нельзя: в нём
-- бывают персональные данные, и таблица служебного механизма стала бы вторым
-- местом, где они лежат, — со своим сроком, своей выгрузкой и своим удалением.
-- Отпечатка достаточно для единственного вопроса, который к нему есть: «то же
-- самое тело или другое».
--
-- state — два значения и ни одного больше:
--
--   in_progress  ключ занят, операция идёт. Второй такой же запрос ждать не
--                заставляют — ему отвечают 409 «повторите позже»: держать
--                соединение открытым, пока чужая транзакция идёт, значит
--                копить их на каждой реплике;
--   completed    операция прошла, ответ сохранён и отдаётся на любой повтор.
--
-- Состояния failed здесь нет и не будет. Транзакция, в которой операция
-- отказала, откатывается ЦЕЛИКОМ вместе со строкой ключа: отказ не сохраняется
-- и не мешает клиенту повторить. Ровно этого мы и хотим — сохранять надо
-- удавшееся, а не всякое.
create table http_idempotency_key (
    tenant_id           uuid        not null,
    key                 text        not null,
    request_fingerprint text        not null,
    state               text        not null,
    response_status     integer,
    response_body       text,
    created_at          timestamptz not null default now(),
    expires_at          timestamptz not null,
    constraint http_idempotency_key_pk primary key (tenant_id, key),
    constraint http_idempotency_key_not_blank check (length(btrim(key)) > 0),
    constraint http_idempotency_key_fits check (length(key) between 8 and 255),
    constraint http_idempotency_key_fingerprint_is_sha256
        check (request_fingerprint ~ '^[0-9a-f]{64}$'),
    constraint http_idempotency_key_state_known
        check (state in ('in_progress', 'completed')),
    -- Завершённый ключ обязан нести ответ, незавершённый — не нести ничего.
    -- Иначе «повтор вернёт сохранённый ответ» держится на аккуратности кода, а
    -- держаться оно должно на схеме: строка completed без ответа — это повтор,
    -- которому нечего отдать, и обнаружится это у клиента.
    constraint http_idempotency_key_completed_has_an_answer check (
        (state = 'completed'
         and response_status is not null
         and response_body is not null
         and response_status between 100 and 599)
        or (state = 'in_progress'
            and response_status is null
            and response_body is null)
    ),
    constraint http_idempotency_key_expires_after_created check (expires_at > created_at)
);
comment on table http_idempotency_key is 'Ключ идемпотентности клиента: отпечаток тела и сохранённый ответ. Повтор с тем же ключом и тем же телом операцию не выполняет.';
comment on column http_idempotency_key.request_fingerprint is 'SHA-256 тела запроса в шестнадцатеричной записи. Тело целиком не хранится: в нём бывают персональные данные, а вопрос к нему один — «то же самое или другое».';
comment on column http_idempotency_key.expires_at is 'Когда ключ перестаёт защищать от повтора. Срок берётся из PDR_IDEMPOTENCY, уборку делает периодическое задание.';

-- Уборке нужен один вопрос: «какие ключи просрочены». Индекс по сроку и
-- отвечает; арендатор в него не входит намеренно — уборка идёт по всем сразу,
-- под ролью миграций, а не из-под приложения.
create index http_idempotency_key_by_age on http_idempotency_key (expires_at);

alter table http_idempotency_key enable row level security;
alter table http_idempotency_key force row level security;

create policy http_idempotency_key_isolation on http_idempotency_key
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. delete не выдаётся: удалять ключи — работа уборки, и
-- идёт она под ролью миграций. Приложение, умеющее удалить ключ, умеет и
-- обойти собственную защиту от повтора — одним лишним запросом.
grant select, insert, update on http_idempotency_key to pdr_app;
