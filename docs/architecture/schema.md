# Схема базы

<!-- Файл собран scripts/gen_schema_doc.py из db/migrations. Руками не правится:
     правка переживёт ровно до следующей пересборки. Изменить схему — значит
     написать новую миграцию. -->

Собрано из миграций: 6. Таблиц: 14.

Правила, которым подчиняется каждая колонка, — в
[migrations.md](migrations.md). Как устроена изоляция арендаторов и почему у
каждой доменной таблицы обязана быть политика — в [tenancy.md](tenancy.md);
отсутствие политики роняет сборку (`scripts/check_rls.py`).

## Таблицы

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

Ограничения:

* `constraint identity_access_log_pk primary key (tenant_id, id)`
* `constraint identity_access_log_actor_fk foreign key (tenant_id, actor_id) references identity_person (tenant_id, id)`
* `constraint identity_access_log_subject_fk foreign key (tenant_id, subject_id) references identity_person (tenant_id, id)`
* `constraint identity_access_log_not_self check (actor_id <> subject_id)`
* `constraint identity_access_log_kind_known check (resource_kind in ( , , ))`

Индексы:

* `identity_access_log_by_subject` — обычный, `(tenant_id, subject_id, at desc)`

Построчная защита включена и форсирована.

Политики:

* `identity_access_log_isolation` — `using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid) with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)`

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

### identity_tenant

Арендатор: репетитор-одиночка или школа. Его собственный идентификатор и есть tenant_id, поэтому политика изоляции на этой таблице такая же, как на остальных.

Заведена миграцией `V002__init.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `tenant_id` | `uuid` | uuid not null |
| `name` | `text` | text not null |
| `tz` | `text` | text not null |
| `created_at` | `timestamptz` | timestamptz not null default now() |

Ограничения:

* `constraint identity_tenant_pk primary key (tenant_id)`
* `constraint identity_tenant_name_not_blank check (length(btrim(name)) > 0)`
* `constraint identity_tenant_tz_not_blank check (length(btrim(tz)) > 0)`

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
