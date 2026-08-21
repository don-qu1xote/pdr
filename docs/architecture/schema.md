# Схема базы

<!-- Файл собран scripts/gen_schema_doc.py из db/migrations. Руками не правится:
     правка переживёт ровно до следующей пересборки. Изменить схему — значит
     написать новую миграцию. -->

Собрано из миграций: 2. Таблиц: 5.

Правила, которым подчиняется каждая колонка, — в
[migrations.md](migrations.md). Как устроена изоляция арендаторов и почему у
каждой доменной таблицы обязана быть политика — в [tenancy.md](tenancy.md);
отсутствие политики роняет сборку (`scripts/check_rls.py`).

## Таблицы

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
