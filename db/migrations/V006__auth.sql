-- Аутентификация: кто это.
--
-- Права здесь не хранятся и не проверяются — это отдельный вопрос и отдельная
-- задача (PDR-IDENT-03). Здесь только «тот ли это, за кого себя выдаёт».
--
-- ЧЕТЫРЕ ТАБЛИЦЫ, И У КАЖДОЙ ЕСТЬ АРЕНДАТОР. В том числе у счётчика попыток
-- входа: попытку делают по чьей-то форме входа, а не в пустоту. Из этого
-- следует то, ради чего стоит читать этот абзац: К МОМЕНТУ ЛЮБОГО ЗАПРОСА
-- АРЕНДАТОР УЖЕ ИЗВЕСТЕН. Почта уникальна ВНУТРИ арендатора
-- (identity_person_email_unique), поэтому «найти человека по одной почте» в
-- этой схеме не выражается вовсе, и вход, приглашение и сброс несут арендатора
-- с собой — иначе пришлось бы читать эти таблицы в обход построчной защиты.
--
-- Владение таблицами — docs/architecture/context-map.md (контекст identity).
-- Как это работает целиком — docs/architecture/authentication.md.

-- Пароль. Отдельно от человека, потому что человек без пароля — обычное дело:
-- ученика завели, а по ссылке приглашения он ещё не пришёл. Это отсутствие
-- строки, а не пустая колонка.
create table identity_credential (
    tenant_id     uuid        not null references identity_tenant (tenant_id),
    person_id     uuid        not null,
    password_hash text        not null,
    updated_at    timestamptz not null default now(),
    constraint identity_credential_pk primary key (tenant_id, person_id),
    constraint identity_credential_person_fk
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    -- Argon2id и ничего другого. Параметры счёта лежат ВНУТРИ записи, поэтому
    -- смена стоимости в конфиге не мешает проверять старые хеши: иначе смена
    -- стоимости означала бы, что войти больше не может никто.
    constraint identity_credential_argon2id check (password_hash like '$argon2id$%')
);
comment on table identity_credential is 'Хеш пароля человека, Argon2id. Человека без пароля здесь просто нет строки.';

-- Сессия. Серверная, а не подписанный токен у клиента.
--
-- ОТЗЫВ ОБЯЗАН РАБОТАТЬ МГНОВЕННО, и это решает всё остальное. Токен без
-- состояния действует до собственного истечения, и «выйти со всех устройств»,
-- «репетитор отозвал доступ», «опекун отозвал согласие» после него означают
-- «через пятнадцать минут». У нас учатся дети, и пятнадцать минут — не мелочь.
--
-- id — это и есть тот секрет, который едет в cookie (вместе с арендатором,
-- см. заголовок файла). Строка клиента и адрес хранятся ОТПЕЧАТКАМИ: адрес —
-- персональные данные, а на вопрос «то же ли это устройство, что вчера»
-- отпечатка достаточно.
create table identity_session (
    tenant_id       uuid        not null references identity_tenant (tenant_id),
    id              uuid        not null,
    person_id       uuid        not null,
    created_at      timestamptz not null default now(),
    expires_at      timestamptz not null,
    revoked_at      timestamptz,
    user_agent_hash text        not null,
    ip_hash         text        not null,
    constraint identity_session_pk primary key (tenant_id, id),
    constraint identity_session_person_fk
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    constraint identity_session_expires_after_created check (expires_at > created_at),
    constraint identity_session_revoked_after_created
        check (revoked_at is null or revoked_at >= created_at),
    constraint identity_session_agent_hashed check (user_agent_hash ~ '^[0-9a-f]{64}$'),
    constraint identity_session_address_hashed check (ip_hash ~ '^[0-9a-f]{64}$')
);
comment on table identity_session is 'Серверная сессия. Отзыв — строка с датой, действующая немедленно: подписанный токен без состояния так не умеет.';

-- «Погасить все сессии человека» — то, что делают при смене и сбросе пароля.
-- Частичный индекс считает только живые: погашенных со временем станет больше,
-- чем действующих.
create index identity_session_alive_by_person
    on identity_session (tenant_id, person_id)
    where revoked_at is null;

-- Одноразовый токен: приглашение ученика и сброс пароля.
--
-- ОДИН МЕХАНИЗМ НА ДВА СЛУЧАЯ. Правила у них ровно одни: секрет живёт коротко,
-- срабатывает один раз и хранится ОТПЕЧАТКОМ. Две похожие таблицы означали бы,
-- что через полгода срок жизни поправят у одной.
--
-- Самого секрета здесь нет и быть не может: утёкшая копия базы не даёт войти
-- по чужой ссылке, потому что из отпечатка секрет не восстановить.
--
-- Приглашение несёт РОЛЬ (человека ещё нет), сброс несёт ЧЕЛОВЕКА (он давно
-- есть). Одновременно оба не бывают, и это ограничение стоит ниже, а не в
-- договорённости.
create table identity_one_time_token (
    tenant_id  uuid        not null references identity_tenant (tenant_id),
    id         uuid        not null,
    purpose    text        not null,
    token_hash text        not null,
    role       text,
    person_id  uuid,
    created_at timestamptz not null default now(),
    expires_at timestamptz not null,
    used_at    timestamptz,
    constraint identity_one_time_token_pk primary key (tenant_id, id),
    -- Уникальность внутри арендатора, а не глобальная: проверки целостности
    -- идут в обход политики, и глобальный ключ выдавал бы существование чужой
    -- строки сообщением об ошибке. Глобальную неповторимость обеспечивают 256
    -- бит случайности в самом секрете, а не индекс.
    constraint identity_one_time_token_secret_unique unique (tenant_id, token_hash),
    constraint identity_one_time_token_person_fk
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    constraint identity_one_time_token_purpose_known
        check (purpose in ('invitation', 'password_reset')),
    constraint identity_one_time_token_hashed check (token_hash ~ '^[0-9a-f]{64}$'),
    constraint identity_one_time_token_role_known
        check (role is null or role in ('owner', 'tutor', 'student', 'guardian')),
    constraint identity_one_time_token_points_at_one_thing check (
        (purpose = 'invitation' and role is not null and person_id is null) or
        (purpose = 'password_reset' and role is null and person_id is not null)),
    constraint identity_one_time_token_expires_after_created check (expires_at > created_at),
    constraint identity_one_time_token_used_after_created
        check (used_at is null or used_at >= created_at)
);
comment on table identity_one_time_token is 'Приглашение ученика и сброс пароля: один механизм. Хранится отпечаток секрета, сам секрет отдаётся человеку один раз и больше нигде не появляется.';

-- Счётчик неудачных попыток входа.
--
-- В БАЗЕ, А НЕ В ПАМЯТИ ПРОЦЕССА. В памяти он считает попытки одной реплики:
-- со второй репликой порог молча удваивается, а перезапуск обнуляет счёт
-- целиком — то есть защита выключается тем же способом, каким её обходят.
--
-- Первичный ключ естественный, как у jobs_effect: строка на «арендатор, по
-- чему считаем, отпечаток». Прибавление идёт одним insert ... on conflict, и
-- две реплики одновременно дают два, а не один.
--
-- Считается и по учётной записи, и по адресу: порог только по записи не мешает
-- перебирать пароли по всему списку учеников с одной машины, порог только по
-- адресу не мешает подбирать пароль к одной записи из ботнета.
create table identity_login_attempt (
    tenant_id         uuid        not null references identity_tenant (tenant_id),
    subject_kind      text        not null,
    subject_hash      text        not null,
    window_started_at timestamptz not null,
    attempts          integer     not null,
    constraint identity_login_attempt_pk primary key (tenant_id, subject_kind, subject_hash),
    constraint identity_login_attempt_kind_known
        check (subject_kind in ('account', 'address')),
    -- Ни почты, ни адреса в открытом виде: чтобы прибавить единицу, знать их
    -- не нужно.
    constraint identity_login_attempt_hashed check (subject_hash ~ '^[0-9a-f]{64}$'),
    constraint identity_login_attempt_count_positive check (attempts > 0)
);
comment on table identity_login_attempt is 'Неудачные попытки входа по учётной записи и по адресу. Лежит в базе, а не в памяти процесса: реплик бывает больше одной.';

-- Уборка отработавших окон: строка нужна ровно до конца своего окна.
create index identity_login_attempt_by_age on identity_login_attempt (window_started_at);

-- ИЗОЛЯЦИЯ. Правило общее, исключений нет ни у одной из четырёх.
alter table identity_credential enable row level security;
alter table identity_credential force row level security;
alter table identity_session enable row level security;
alter table identity_session force row level security;
alter table identity_one_time_token enable row level security;
alter table identity_one_time_token force row level security;
alter table identity_login_attempt enable row level security;
alter table identity_login_attempt force row level security;

create policy identity_credential_isolation on identity_credential
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_session_isolation on identity_session
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_one_time_token_isolation on identity_one_time_token
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_login_attempt_isolation on identity_login_attempt
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. Поимённо и по делу.
--
-- У identity_session нет delete: погашенная сессия остаётся строкой с датой
-- отзыва. «Когда этот доступ забрали» — вопрос, который задают после того, как
-- что-то случилось, и отвечать на него должно хранилище, а не память.
--
-- У identity_one_time_token нет delete по той же причине: сработавшая ссылка
-- остаётся с датой, иначе «по ней уже заходили» и «такой ссылки не было»
-- перестают различаться.
--
-- У identity_login_attempt delete есть — это уборка отработавших окон и
-- забывание счёта после удачного входа.
grant select, insert, update on identity_credential to pdr_app;
grant select, insert, update on identity_session to pdr_app;
grant select, insert, update on identity_one_time_token to pdr_app;
grant select, insert, update, delete on identity_login_attempt to pdr_app;
