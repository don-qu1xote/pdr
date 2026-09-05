# Схема базы

<!-- Файл собран scripts/gen_schema_doc.py из db/migrations. Руками не правится:
     правка переживёт ровно до следующей пересборки. Изменить схему — значит
     написать новую миграцию. -->

Собрано из миграций: 13. Таблиц: 26.

Правила, которым подчиняется каждая колонка, — в
[migrations.md](migrations.md). Как устроена изоляция арендаторов и почему у
каждой доменной таблицы обязана быть политика — в [tenancy.md](tenancy.md);
отсутствие политики роняет сборку (`scripts/check_rls.py`).

## Таблицы

### http_idempotency_key

Ключ идемпотентности клиента: отпечаток тела и сохранённый ответ. Повтор с тем же ключом и тем же телом операцию не выполняет.

Заведена миграцией `V010__idempotency.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `key` | `text` | text not null |
| `request_fingerprint` | `text` | text not null |
| `state` | `text` | text not null |
| `response_status` | `integer` | integer |
| `response_body` | `text` | text |
| `created_at` | `timestamptz` | timestamptz not null default now() |
| `expires_at` | `timestamptz` | timestamptz not null |

Ограничения:

* `constraint http_idempotency_key_pk primary key (tenant_id, key)`
* `constraint http_idempotency_key_not_blank check (length(btrim(key)) > 0)`
* `constraint http_idempotency_key_fits check (length(key) between 8 and 255)`
* `constraint http_idempotency_key_fingerprint_is_sha256 check (request_fingerprint ~ )`
* `constraint http_idempotency_key_state_known check (state in ( , ))`
* `constraint http_idempotency_key_completed_has_an_answer check ( (state = and response_status is not null and response_body is not null and response_status between 100 and 599) or (state = and response_status is null and response_body is null) )`
* `constraint http_idempotency_key_expires_after_created check (expires_at > created_at)`

Индексы:

* `http_idempotency_key_by_age` — обычный, `(expires_at)`

Построчная защита включена и форсирована.

Политики:

* `http_idempotency_key_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_access_log

Кто и когда смотрел запись занятия, транскрипт или переписку. Право смотреть не то же самое, что право смотреть незаметно.

Заведена миграцией `V005__access_log.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `actor_id` | `uuid` | uuid not null |
| `subject_id` | `uuid` | uuid not null |
| `resource_kind` | `text` | text not null |
| `at` | `timestamptz` | timestamptz not null default now() |
| `outcome` | `text` | text not null default |

Ограничения:

* `constraint identity_access_log_pk primary key (tenant_id, id)`
* `constraint identity_access_log_actor_fk foreign key (tenant_id, actor_id) references identity_person (tenant_id, id)`
* `constraint identity_access_log_subject_fk foreign key (tenant_id, subject_id) references identity_person (tenant_id, id)`
* `constraint identity_access_log_not_self check (actor_id <> subject_id)`
* `constraint identity_access_log_kind_known check (resource_kind in ( , , ))`
* `constraint identity_access_log_outcome_known check (outcome in ( , ))`

Индексы:

* `identity_access_log_by_subject` — обычный, `(tenant_id, subject_id, at desc)`

Построчная защита включена и форсирована.

Политики:

* `identity_access_log_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_account

Один человек на всю площадку: отпечаток почты и идентификатор. Единственная таблица без tenant_id — ADR-0019.

Заведена миграцией `V008__practice_and_accounts.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `id` | `uuid` | uuid not null |
| `email_digest` | `char(64)` | char(64) not null |
| `confirmed_at` | `timestamptz` | timestamptz |
| `confirmation_digest` | `char(64)` | char(64) |
| `confirmation_expires_at` | `timestamptz` | timestamptz |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint identity_account_pk primary key (id)`
* `constraint identity_account_mail_unique unique (email_digest)`
* `constraint identity_account_digest_lowercase check (email_digest = lower(email_digest))`
* `constraint identity_account_confirmation_cleared check (confirmed_at is null or confirmation_digest is null)`
* `constraint identity_account_confirmation_whole check ((confirmation_digest is null) = (confirmation_expires_at is null))`

Не доменная таблица: один человек на всю площадку: отпечаток почты и идентификатор (ADR-0019). Арендатора и политики у неё нет.

### identity_consent

Согласие на обработку по перечню и на запись занятий: кто дал, когда, какую версию и каким действием. За ребёнка соглашается опекун.

Заведена миграцией `V011__consent.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `id` | `uuid` | uuid not null |
| `subject_id` | `uuid` | uuid not null |
| `given_by` | `uuid` | uuid not null |
| `kind` | `text` | text not null |
| `version` | `integer` | integer not null |
| `action` | `text` | text not null |
| `given_at` | `timestamptz` | timestamptz not null default now() |
| `withdrawn_at` | `timestamptz` | timestamptz |

Ограничения:

* `constraint identity_consent_pk primary key (tenant_id, id)`
* `constraint identity_consent_subject_fk foreign key (tenant_id, subject_id) references identity_person (tenant_id, id)`
* `constraint identity_consent_given_by_fk foreign key (tenant_id, given_by) references identity_person (tenant_id, id)`
* `constraint identity_consent_kind_known check (kind in ( , ))`
* `constraint identity_consent_action_known check (action in ( , , ))`
* `constraint identity_consent_version_from_one check (version >= 1)`
* `constraint identity_consent_withdrawn_after_given check (withdrawn_at is null or withdrawn_at >= given_at)`

Индексы:

* `identity_consent_live` — уникальный, `(tenant_id, subject_id, kind) where withdrawn_at is null`
* `identity_consent_by_subject` — обычный, `(tenant_id, subject_id, given_at desc)`

Построчная защита включена и форсирована.

Политики:

* `identity_consent_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_credential

Хеш пароля человека, Argon2id. Человека без пароля здесь просто нет строки.

Заведена миграцией `V006__auth.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `person_id` | `uuid` | uuid not null |
| `password_hash` | `text` | text not null |
| `updated_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint identity_credential_pk primary key (tenant_id, person_id)`
* `constraint identity_credential_person_fk foreign key (tenant_id, person_id) references identity_person (tenant_id, id)`
* `constraint identity_credential_argon2id check (password_hash like )`

Построчная защита включена и форсирована.

Политики:

* `identity_credential_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_guardian_consent

Согласие на один уровень доступа опекуна. Отзыв — строка с датой: журнал обязан отвечать на «кто имел доступ в марте».

Заведена миграцией `V007__guardian_access.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `guardian_id` | `uuid` | uuid not null |
| `student_id` | `uuid` | uuid not null |
| `scope` | `text` | text not null |
| `granted_at` | `timestamptz` | timestamptz not null default now() |
| `granted_by` | `uuid` | uuid not null |
| `expires_at` | `timestamptz` | timestamptz |
| `revoked_at` | `timestamptz` | timestamptz |
| `revoked_by` | `uuid` | uuid |
| `basis` | `text` | text not null default |

Ограничения:

* `constraint identity_guardian_consent_pk primary key (tenant_id, id)`
* `constraint identity_guardian_consent_guardian_fk foreign key (tenant_id, guardian_id) references identity_person (tenant_id, id)`
* `constraint identity_guardian_consent_student_fk foreign key (tenant_id, student_id) references identity_person (tenant_id, id)`
* `constraint identity_guardian_consent_granted_by_fk foreign key (tenant_id, granted_by) references identity_person (tenant_id, id)`
* `constraint identity_guardian_consent_revoked_by_fk foreign key (tenant_id, revoked_by) references identity_person (tenant_id, id)`
* `constraint identity_guardian_consent_not_self check (guardian_id <> student_id)`
* `constraint identity_guardian_consent_scope_known check (scope in ( , , , ))`
* `constraint identity_guardian_consent_expires_after_granted check (expires_at is null or expires_at > granted_at)`
* `constraint identity_guardian_consent_revoked_after_granted check (revoked_at is null or revoked_at >= granted_at)`
* `constraint identity_guardian_consent_revoked_by_someone check ((revoked_at is null) = (revoked_by is null))`
* `constraint identity_guardian_consent_basis_known check (basis in ( , , ))`
* `constraint identity_guardian_consent_money_is_not_sight check (basis <> or scope = )`

Индексы:

* `identity_guardian_consent_active` — уникальный, `(tenant_id, guardian_id, student_id, scope) where revoked_at is null`

Построчная защита включена и форсирована.

Политики:

* `identity_guardian_consent_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_guardianship

Опека: кто вправе действовать от имени ученика. Отзыв не удаляет связь, а проставляет дату.

Заведена миграцией `V002__init.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `guardian_id` | `uuid` | uuid not null |
| `student_id` | `uuid` | uuid not null |
| `granted_at` | `timestamptz` | timestamptz not null default now() |
| `revoked_at` | `timestamptz` | timestamptz |

Ограничения:

* `constraint identity_guardianship_pk primary key (tenant_id, id)`
* `constraint identity_guardianship_guardian_fk foreign key (tenant_id, guardian_id) references identity_person (tenant_id, id)`
* `constraint identity_guardianship_student_fk foreign key (tenant_id, student_id) references identity_person (tenant_id, id)`
* `constraint identity_guardianship_not_self check (guardian_id <> student_id)`
* `constraint identity_guardianship_revoked_after_granted check (revoked_at is null or revoked_at >= granted_at)`

Индексы:

* `identity_guardianship_active` — уникальный, `(tenant_id, guardian_id, student_id) where revoked_at is null`
* `identity_guardianship_by_student` — обычный, `(tenant_id, student_id)`

Построчная защита включена и форсирована.

Политики:

* `identity_guardianship_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_login_attempt

Неудачные попытки входа по учётной записи и по адресу. Лежит в базе, а не в памяти процесса: реплик бывает больше одной.

Заведена миграцией `V006__auth.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `subject_kind` | `text` | text not null |
| `subject_hash` | `text` | text not null |
| `window_started_at` | `timestamptz` | timestamptz not null |
| `attempts` | `integer` | integer not null |

Ограничения:

* `constraint identity_login_attempt_pk primary key (tenant_id, subject_kind, subject_hash)`
* `constraint identity_login_attempt_kind_known check (subject_kind in ( , ))`
* `constraint identity_login_attempt_hashed check (subject_hash ~ )`
* `constraint identity_login_attempt_count_positive check (attempts > 0)`

Индексы:

* `identity_login_attempt_by_age` — обычный, `(window_started_at)`

Построчная защита включена и форсирована.

Политики:

* `identity_login_attempt_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_one_time_token

Приглашение ученика и сброс пароля: один механизм. Хранится отпечаток секрета, сам секрет отдаётся человеку один раз и больше нигде не появляется.

Заведена миграцией `V006__auth.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `purpose` | `text` | text not null |
| `token_hash` | `text` | text not null |
| `role` | `text` | text |
| `person_id` | `uuid` | uuid |
| `created_at` | `timestamptz` | timestamptz not null default now() |
| `expires_at` | `timestamptz` | timestamptz not null |
| `used_at` | `timestamptz` | timestamptz |
| `invited_digest` | `char(64)` | char(64) |

Ограничения:

* `constraint identity_one_time_token_pk primary key (tenant_id, id)`
* `constraint identity_one_time_token_secret_unique unique (tenant_id, token_hash)`
* `constraint identity_one_time_token_person_fk foreign key (tenant_id, person_id) references identity_person (tenant_id, id)`
* `constraint identity_one_time_token_purpose_known check (purpose in ( , ))`
* `constraint identity_one_time_token_hashed check (token_hash ~ )`
* `constraint identity_one_time_token_role_known check (role is null or role in ( , , , ))`
* `constraint identity_one_time_token_points_at_one_thing check ( (purpose = and role is not null and person_id is null) or (purpose = and role is null and person_id is not null))`
* `constraint identity_one_time_token_expires_after_created check (expires_at > created_at)`
* `constraint identity_one_time_token_used_after_created check (used_at is null or used_at >= created_at)`
* `constraint identity_one_time_token_invited_lowercase check (invited_digest is null or invited_digest = lower(invited_digest))`

Индексы:

* `identity_one_time_token_invited` — обычный, `(tenant_id, invited_digest) where invited_digest is not null and used_at is null`

Построчная защита включена и форсирована.

Политики:

* `identity_one_time_token_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_person

Человек и его контакты. Составной ключ (tenant_id, id) не даёт сослаться на человека из чужого тенанта.

Заведена миграцией `V002__init.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `display_name` | `text` | text not null |
| `email` | `text` | text |
| `tz` | `text` | text not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |
| `born_on` | `date` | date |

Ограничения:

* `constraint identity_person_pk primary key (tenant_id, id)`
* `constraint identity_person_email_unique unique (tenant_id, email)`
* `constraint identity_person_display_name_not_blank check (length(btrim(display_name)) > 0)`
* `constraint identity_person_email_lowercase check (email is null or email = lower(email))`
* `constraint identity_person_tz_not_blank check (length(btrim(tz)) > 0)`

Индексы:

* `identity_person_by_tenant` — обычный, `(tenant_id)`

Построчная защита включена и форсирована.

Политики:

* `identity_person_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_role_assignment

Роль человека в тенанте: владелец, репетитор, ученик, опекун. Отозванная роль остаётся строкой с датой отзыва.

Заведена миграцией `V002__init.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `person_id` | `uuid` | uuid not null |
| `role` | `text` | text not null |
| `granted_at` | `timestamptz` | timestamptz not null default now() |
| `revoked_at` | `timestamptz` | timestamptz |

Ограничения:

* `constraint identity_role_assignment_pk primary key (tenant_id, id)`
* `constraint identity_role_assignment_person_fk foreign key (tenant_id, person_id) references identity_person (tenant_id, id)`
* `constraint identity_role_assignment_role_known check (role in ( , , , ))`
* `constraint identity_role_assignment_revoked_after_granted check (revoked_at is null or revoked_at >= granted_at)`

Индексы:

* `identity_role_assignment_active` — уникальный, `(tenant_id, person_id, role) where revoked_at is null`
* `identity_role_assignment_by_person` — обычный, `(tenant_id, person_id)`

Построчная защита включена и форсирована.

Политики:

* `identity_role_assignment_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_session

Серверная сессия. Отзыв — строка с датой, действующая немедленно: подписанный токен без состояния так не умеет.

Заведена миграцией `V006__auth.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `person_id` | `uuid` | uuid not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |
| `expires_at` | `timestamptz` | timestamptz not null |
| `revoked_at` | `timestamptz` | timestamptz |
| `user_agent_hash` | `text` | text not null |
| `ip_hash` | `text` | text not null |

Ограничения:

* `constraint identity_session_pk primary key (tenant_id, id)`
* `constraint identity_session_person_fk foreign key (tenant_id, person_id) references identity_person (tenant_id, id)`
* `constraint identity_session_expires_after_created check (expires_at > created_at)`
* `constraint identity_session_revoked_after_created check (revoked_at is null or revoked_at >= created_at)`
* `constraint identity_session_agent_hashed check (user_agent_hash ~ )`
* `constraint identity_session_address_hashed check (ip_hash ~ )`

Индексы:

* `identity_session_alive_by_person` — обычный, `(tenant_id, person_id) where revoked_at is null`

Построчная защита включена и форсирована.

Политики:

* `identity_session_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### identity_signup_attempt

Сколько раз с этого адреса заводились сами. Ни почты, ни адреса в открытом виде — только отпечаток.

Заведена миграцией `V008__practice_and_accounts.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `address_hash` | `char(64)` | char(64) not null |
| `window_started_at` | `timestamptz` | timestamptz not null |
| `attempts` | `integer` | integer not null |

Ограничения:

* `constraint identity_signup_attempt_pk primary key (address_hash)`
* `constraint identity_signup_attempt_positive check (attempts > 0)`
* `constraint identity_signup_attempt_hash_lowercase check (address_hash = lower(address_hash))`

Индексы:

* `identity_signup_attempt_by_age` — обычный, `(window_started_at)`

Не доменная таблица: счётчик самостоятельных заведений с одного адреса, до всякого арендатора. Арендатора и политики у неё нет.

### identity_tenant

Арендатор: репетитор-одиночка или школа. Его собственный идентификатор и есть tenant_id, поэтому политика изоляции на этой таблице такая же, как на остальных.

Заведена миграцией `V002__init.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `name` | `text` | text not null |
| `tz` | `text` | text not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |
| `visibility` | `text` | text not null default |
| `visibility_asked_at` | `timestamptz` | timestamptz |
| `visibility_decided_at` | `timestamptz` | timestamptz |
| `visibility_refusal` | `text` | text |

Ограничения:

* `constraint identity_tenant_pk primary key (tenant_id)`
* `constraint identity_tenant_name_not_blank check (length(btrim(name)) > 0)`
* `constraint identity_tenant_tz_not_blank check (length(btrim(tz)) > 0)`
* `constraint identity_tenant_visibility_known check (visibility in ( , , , ))`
* `constraint identity_tenant_refusal_known check (visibility_refusal is null or visibility_refusal in ( , , ))`
* `constraint identity_tenant_refusal_only_when_refused check (visibility_refusal is null or visibility = )`

Индексы:

* `identity_tenant_awaiting_review` — обычный, `(visibility_asked_at) where visibility = 'pending'`

Построчная защита включена и форсирована.

Политики:

* `identity_tenant_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### jobs_effect

След уже произведённого действия задания: (арендатор, задание, ключ действия). Повтор после потери блокировки упирается в первичный ключ и второго действия не производит.

Заведена миграцией `V003__jobs.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `job` | `text` | text not null |
| `effect_key` | `text` | text not null |
| `produced_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint jobs_effect_pk primary key (tenant_id, job, effect_key)`
* `constraint jobs_effect_job_not_blank check (length(btrim(job)) > 0)`
* `constraint jobs_effect_key_not_blank check (length(btrim(effect_key)) > 0)`

Индексы:

* `jobs_effect_by_age` — обычный, `(produced_at)`

Построчная защита включена и форсирована.

Политики:

* `jobs_effect_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### jobs_lock

Распределённая блокировка периодических заданий: одна строка на задание во всём кластере. Форму таблицы задаёт штатный storages::postgres::DistLock.

Заведена миграцией `V003__jobs.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `key` | `text` | text not null |
| `owner` | `text` | text |
| `expiration_time` | `timestamptz` | timestamptz |

Ограничения:

* `constraint jobs_lock_pk primary key (key)`
* `constraint jobs_lock_key_not_blank check (length(btrim(key)) > 0)`

Не доменная таблица: распределённая блокировка периодических заданий, одна на кластер. Арендатора и политики у неё нет.

### jobs_run

Последний прогон задания: начало попытки, начало и конец последнего завершённого прогона, длительность и исход. Возраст этой записи — метрика живости задания.

Заведена миграцией `V003__jobs.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `job` | `text` | text not null |
| `attempt_at` | `timestamptz` | timestamptz not null |
| `started_at` | `timestamptz` | timestamptz |
| `finished_at` | `timestamptz` | timestamptz |
| `duration_ms` | `bigint` | bigint |
| `outcome` | `text` | text not null |
| `produced` | `bigint` | bigint not null default 0 |
| `repeated` | `bigint` | bigint not null default 0 |
| `runs` | `bigint` | bigint not null default 0 |

Ограничения:

* `constraint jobs_run_pk primary key (job)`
* `constraint jobs_run_job_not_blank check (length(btrim(job)) > 0)`
* `constraint jobs_run_outcome_known check (outcome in ( , , , ))`
* `constraint jobs_run_finished_is_whole check ((finished_at is null) = (started_at is null) and (finished_at is null) = (duration_ms is null))`
* `constraint jobs_run_duration_not_negative check (duration_ms is null or duration_ms >= 0)`
* `constraint jobs_run_finished_after_started check (finished_at is null or finished_at >= started_at)`
* `constraint jobs_run_counters_not_negative check (produced >= 0 and repeated >= 0 and runs >= 0)`

Не доменная таблица: журнал последнего прогона задания, один на кластер. Арендатора и политики у неё нет.

### observability_product_event

Поток продуктовых событий: что сделал человек, обезличенно. Ссылка на арендатора и роль, идентификатора человека нет ни колонкой, ни ключом в fields. Отдельно от технических метрик: у них разные читатели, права и срок жизни.

Заведена миграцией `V004__observability.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null references identity_tenant (tenant_id) |
| `id` | `uuid` | uuid not null |
| `type` | `text` | text not null |
| `version` | `integer` | integer not null |
| `actor_role` | `text` | text not null |
| `occurred_at` | `timestamptz` | timestamptz not null |
| `recorded_at` | `timestamptz` | timestamptz not null default now() |
| `fields` | `jsonb` | jsonb not null |

Ограничения:

* `constraint observability_product_event_pk primary key (tenant_id, id)`
* `constraint observability_product_event_type_shaped check (type ~ )`
* `constraint observability_product_event_version_from_one check (version >= 1)`
* `constraint observability_product_event_role_known check (actor_role in ( , , , ))`
* `constraint observability_product_event_fields_are_object check (jsonb_typeof(fields) = )`
* `constraint observability_product_event_fields_are_anonymous check (not jsonb_path_exists( fields, ))`
* `constraint observability_product_event_recorded_after_occurred check (recorded_at >= occurred_at)`

Индексы:

* `observability_product_event_by_age` — обычный, `(recorded_at)`
* `observability_product_event_by_type` — обычный, `(tenant_id, type, occurred_at)`

Построчная защита включена и форсирована.

Политики:

* `observability_product_event_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_availability

Когда репетитор готов работать: день недели и часы ПО ЕГО ЧАСАМ. Зона рядом, потому что «с десяти» — это утверждение про его часы.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `id` | `uuid` | uuid not null |
| `tutor_id` | `uuid` | uuid not null |
| `weekday` | `smallint` | smallint not null |
| `from_minute` | `smallint` | smallint not null |
| `to_minute` | `smallint` | smallint not null |
| `tz` | `text` | text not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint scheduling_availability_pk primary key (tenant_id, id)`
* `constraint scheduling_availability_weekday_known check (weekday between 0 and 6)`
* `constraint scheduling_availability_from_on_the_clock check (from_minute between 0 and 1439)`
* `constraint scheduling_availability_to_on_the_clock check (to_minute between 1 and 1440)`
* `constraint scheduling_availability_forward check (to_minute > from_minute)`
* `constraint scheduling_availability_tz_named check (length(btrim(tz)) > 0)`

Индексы:

* `scheduling_availability_by_tutor` — обычный, `(tenant_id, tutor_id)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_availability_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_availability_exception

День, который живёт не по правилу: выходной (без часов) или иные часы. Сильнее недельного правила.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `tutor_id` | `uuid` | uuid not null |
| `on_date` | `date` | date not null |
| `starts_at` | `timestamptz` | timestamptz |
| `ends_at` | `timestamptz` | timestamptz |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint scheduling_availability_exception_pk primary key (tenant_id, tutor_id, on_date)`
* `constraint scheduling_availability_exception_whole check ((starts_at is null) = (ends_at is null))`
* `constraint scheduling_availability_exception_forward check (ends_at is null or ends_at > starts_at)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_availability_exception_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_lesson

Занятие: два момента в UTC, зона задумки рядом и состояние из закрытого списка. Пересечения у репетитора запрещены самой базой.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `id` | `uuid` | uuid not null |
| `series_id` | `uuid` | uuid |
| `tutor_id` | `uuid` | uuid not null |
| `starts_at` | `timestamptz` | timestamptz not null |
| `ends_at` | `timestamptz` | timestamptz not null |
| `tz` | `text` | text not null |
| `state` | `text` | text not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint scheduling_lesson_pk primary key (tenant_id, id)`
* `constraint scheduling_lesson_forward check (ends_at > starts_at)`
* `constraint scheduling_lesson_tz_named check (length(btrim(tz)) > 0)`
* `constraint scheduling_lesson_state_known check (state in ( , , , , ))`
* `constraint scheduling_lesson_no_overlap exclude using gist ( tenant_id with =, tutor_id with =, tstzrange(starts_at, ends_at) with && ) where (state in ( , ))`

Индексы:

* `scheduling_lesson_by_tutor` — обычный, `(tenant_id, tutor_id, starts_at)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_lesson_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_lesson_participant

Кто занимается на этом занятии. Отдельная таблица, потому что участник в домене — вектор.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `lesson_id` | `uuid` | uuid not null |
| `participant_id` | `uuid` | uuid not null |

Ограничения:

* `constraint scheduling_lesson_participant_pk primary key (tenant_id, lesson_id, participant_id)`
* `constraint scheduling_lesson_participant_lesson foreign key (tenant_id, lesson_id) references scheduling_lesson (tenant_id, id) on delete cascade`

Индексы:

* `scheduling_lesson_by_participant` — обычный, `(tenant_id, participant_id, lesson_id)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_lesson_participant_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_series

Регулярные занятия правилом RRULE, а не списком. Время задано по часам репетитора: минуты от полуночи плюс зона.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `id` | `uuid` | uuid not null |
| `tutor_id` | `uuid` | uuid not null |
| `rrule` | `text` | text not null |
| `starts_on` | `date` | date not null |
| `at_minute` | `smallint` | smallint not null |
| `tz` | `text` | text not null |
| `duration_minutes` | `integer` | integer not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint scheduling_series_pk primary key (tenant_id, id)`
* `constraint scheduling_series_rrule_written check (length(btrim(rrule)) > 0)`
* `constraint scheduling_series_at_on_the_clock check (at_minute between 0 and 1439)`
* `constraint scheduling_series_tz_named check (length(btrim(tz)) > 0)`
* `constraint scheduling_series_duration_positive check (duration_minutes > 0)`

Индексы:

* `scheduling_series_by_tutor` — обычный, `(tenant_id, tutor_id)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_series_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_series_exception

Отменённое или перенесённое вхождение серии. Опознаётся местной датой: момент при переводе часов меняется, дата — нет.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `series_id` | `uuid` | uuid not null |
| `occurrence_on` | `date` | date not null |
| `kind` | `text` | text not null |
| `moved_to` | `timestamptz` | timestamptz |
| `moved_minutes` | `integer` | integer |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint scheduling_series_exception_pk primary key (tenant_id, series_id, occurrence_on)`
* `constraint scheduling_series_exception_series foreign key (tenant_id, series_id) references scheduling_series (tenant_id, id) on delete cascade`
* `constraint scheduling_series_exception_kind_known check (kind in ( , ))`
* `constraint scheduling_series_exception_place_matches_kind check ((kind = ) = (moved_to is not null))`
* `constraint scheduling_series_exception_moved_length check (moved_minutes is null or moved_minutes > 0)`
* `constraint scheduling_series_exception_length_needs_place check (moved_minutes is null or moved_to is not null)`

Построчная защита включена и форсирована.

Политики:

* `scheduling_series_exception_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### scheduling_series_participant

Кто занимается по этой серии. Отдельная таблица по той же причине, что и у занятия.

Заведена миграцией `V013__scheduling.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `series_id` | `uuid` | uuid not null |
| `participant_id` | `uuid` | uuid not null |

Ограничения:

* `constraint scheduling_series_participant_pk primary key (tenant_id, series_id, participant_id)`
* `constraint scheduling_series_participant_series foreign key (tenant_id, series_id) references scheduling_series (tenant_id, id) on delete cascade`

Построчная защита включена и форсирована.

Политики:

* `scheduling_series_participant_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

### schema_version

Применённые миграции: версия, момент применения в UTC и контрольная сумма файла.

Заведена миграцией `V001__schema_version.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `version` | `integer` | integer primary key |
| `applied_at` | `timestamptz` | timestamptz not null default now() |
| `checksum` | `char(64)` | char(64) not null |

Не доменная таблица: реестр применённых миграций. Арендатора и политики у неё нет.

## Порядок применения

1. `V001__schema_version.sql` — schema_version
1. `V002__init.sql` — identity_tenant, identity_person, identity_role_assignment, identity_guardianship
1. `V003__jobs.sql` — jobs_lock, jobs_run, jobs_effect
1. `V004__observability.sql` — observability_product_event
1. `V005__access_log.sql` — identity_access_log
1. `V006__auth.sql` — identity_credential, identity_session, identity_one_time_token, identity_login_attempt
1. `V007__guardian_access.sql` — identity_guardian_consent
1. `V008__practice_and_accounts.sql` — identity_account, identity_signup_attempt
1. `V009__consent_basis.sql` — без новых таблиц
1. `V010__idempotency.sql` — http_idempotency_key
1. `V011__consent.sql` — identity_consent
1. `V012__system_tenant.sql` — без новых таблиц
1. `V013__scheduling.sql` — scheduling_availability, scheduling_availability_exception, scheduling_lesson, scheduling_lesson_participant, scheduling_series, scheduling_series_participant, scheduling_series_exception
