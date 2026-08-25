-- Первая схема предметной области и изоляция арендаторов.
--
-- Здесь заводятся четыре таблицы контекста identity и включается построчная
-- защита. Правило, ради которого всё это существует, одно: запрос без условия
-- по арендатору не увидит чужого. Не «увидит меньше» — не увидит ничего.
--
-- Владение таблицами — docs/architecture/context-map.md.
-- Как устроена изоляция и чего она не покрывает — docs/architecture/tenancy.md.
-- Правила колонок — docs/architecture/migrations.md.

-- Роль приложения. Не владелец таблиц, не суперпользователь и без права
-- обходить политику: под ней ходит сервис, и политика для неё обязательна.
--
-- Входить ей нельзя (nologin): пароль — секрет, а секретов в миграциях не
-- бывает. Логин-роль заводится снаружи и включается в эту членством
-- (docs/architecture/tenancy.md).
--
-- Роль живёт в кластере, а не в базе, поэтому «уже есть» — не ошибка: вторая
-- база того же кластера застанет её готовой.
do $role$
begin
    create role pdr_app nologin nobypassrls;
exception
    when duplicate_object then null;
end
$role$;

-- Арендатор. Отдельной колонки tenant_id здесь нет намеренно: собственный
-- идентификатор арендатора и есть его tenant_id, поэтому та же самая политика
-- работает и на этой таблице — арендатор видит ровно свою строку.
create table identity_tenant (
    tenant_id  uuid        not null,
    name       text        not null,
    tz         text        not null,
    created_at timestamptz not null default now(),
    constraint identity_tenant_pk primary key (tenant_id),
    constraint identity_tenant_name_not_blank check (length(btrim(name)) > 0),
    constraint identity_tenant_tz_not_blank check (length(btrim(tz)) > 0)
);
comment on table identity_tenant is 'Арендатор: репетитор-одиночка или школа. Его собственный идентификатор и есть tenant_id, поэтому политика изоляции на этой таблице такая же, как на остальных.';

-- Человек: репетитор, ученик, опекун, самостоятельный взрослый ученик.
--
-- Первичный ключ составной — (tenant_id, id), и это не украшение. Он делает
-- ссылку через границу арендатора невозможной: внешние ключи ниже ведут на обе
-- колонки сразу, поэтому «опекун из чужого тенанта» не выражается в схеме.
-- Заодно уникальность считается внутри арендатора: проверки целостности идут
-- в обход политики, и глобальный уникальный ключ выдавал бы существование
-- чужой строки сообщением об ошибке.
create table identity_person (
    tenant_id    uuid        not null references identity_tenant (tenant_id),
    id           uuid        not null,
    display_name text        not null,
    email        text,
    tz           text        not null,
    created_at   timestamptz not null default now(),
    constraint identity_person_pk primary key (tenant_id, id),
    constraint identity_person_email_unique unique (tenant_id, email),
    constraint identity_person_display_name_not_blank check (length(btrim(display_name)) > 0),
    -- Почта хранится уже приведённой к нижнему регистру: тогда уникальность
    -- выше не обходится сменой регистра, и функционального индекса не нужно.
    constraint identity_person_email_lowercase check (email is null or email = lower(email)),
    constraint identity_person_tz_not_blank check (length(btrim(tz)) > 0)
);
comment on table identity_person is 'Человек и его контакты. Составной ключ (tenant_id, id) не даёт сослаться на человека из чужого тенанта.';

-- Роль человека в тенанте. Отзыв — не удаление: по выданной роли принимались
-- решения, и след о ней остаётся с датой отзыва.
create table identity_role_assignment (
    tenant_id  uuid        not null references identity_tenant (tenant_id),
    id         uuid        not null,
    person_id  uuid        not null,
    role       text        not null,
    granted_at timestamptz not null default now(),
    revoked_at timestamptz,
    constraint identity_role_assignment_pk primary key (tenant_id, id),
    constraint identity_role_assignment_person_fk
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    constraint identity_role_assignment_role_known
        check (role in ('owner', 'tutor', 'student', 'guardian')),
    constraint identity_role_assignment_revoked_after_granted
        check (revoked_at is null or revoked_at >= granted_at)
);
comment on table identity_role_assignment is 'Роль человека в тенанте: владелец, репетитор, ученик, опекун. Отозванная роль остаётся строкой с датой отзыва.';

-- Опека: кто вправе действовать от имени ученика. Домен этой связи уже есть в
-- коде — libs/pdr-identity/src/identity/core/guardianship.hpp.
create table identity_guardianship (
    tenant_id   uuid        not null references identity_tenant (tenant_id),
    id          uuid        not null,
    guardian_id uuid        not null,
    student_id  uuid        not null,
    granted_at  timestamptz not null default now(),
    revoked_at  timestamptz,
    constraint identity_guardianship_pk primary key (tenant_id, id),
    constraint identity_guardianship_guardian_fk
        foreign key (tenant_id, guardian_id) references identity_person (tenant_id, id),
    constraint identity_guardianship_student_fk
        foreign key (tenant_id, student_id) references identity_person (tenant_id, id),
    constraint identity_guardianship_not_self check (guardian_id <> student_id),
    constraint identity_guardianship_revoked_after_granted
        check (revoked_at is null or revoked_at >= granted_at)
);
comment on table identity_guardianship is 'Опека: кто вправе действовать от имени ученика. Отзыв не удаляет связь, а проставляет дату.';

-- Действующая роль у человека одна на каждый вид, действующая опека одна на
-- пару. Отозванных сколько угодно: частичный индекс считает только живые.
create unique index identity_role_assignment_active
    on identity_role_assignment (tenant_id, person_id, role)
    where revoked_at is null;

create unique index identity_guardianship_active
    on identity_guardianship (tenant_id, guardian_id, student_id)
    where revoked_at is null;

-- Обращения всегда идут по арендатору: политика и так отсекает чужое, но без
-- индекса она отсекает его перебором всей таблицы.
create index identity_person_by_tenant on identity_person (tenant_id);
create index identity_role_assignment_by_person on identity_role_assignment (tenant_id, person_id);
create index identity_guardianship_by_student on identity_guardianship (tenant_id, student_id);

-- ИЗОЛЯЦИЯ.
--
-- enable включает политику для всех, кроме владельца таблицы; force не делает
-- исключения и для него. Служебных запросов «в обход» не существует: если
-- запросу нужны данные другого арендатора, он объявляет другого арендатора, а
-- не выключает защиту.
--
-- Параметр сессии не задан — current_setting(..., true) возвращает NULL,
-- сравнение даёт NULL, ни одна строка не подходит. Забытый параметр даёт пустой
-- ответ, а не чужие строки: пустой ответ замечают в тот же день.
alter table identity_tenant enable row level security;
alter table identity_tenant force row level security;
alter table identity_person enable row level security;
alter table identity_person force row level security;
alter table identity_role_assignment enable row level security;
alter table identity_role_assignment force row level security;
alter table identity_guardianship enable row level security;
alter table identity_guardianship force row level security;

-- using отвечает за чтение, изменение и удаление, with check — за запись:
-- без него арендатор мог бы вставить строку, которую сам потом не увидит.
-- nullif спасает от пустой строки: ''::uuid — это ошибка, а не пустой ответ.
create policy identity_tenant_isolation on identity_tenant
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_person_isolation on identity_person
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_role_assignment_isolation on identity_role_assignment
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

create policy identity_guardianship_isolation on identity_guardianship
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. Перечислены поимённо: grant on all tables выдал бы
-- права и на реестр миграций, и на всякую будущую таблицу, включая ту, где
-- политику забыли включить.
grant usage on schema public to pdr_app;
grant select, insert, update, delete on
    identity_tenant,
    identity_person,
    identity_role_assignment,
    identity_guardianship
    to pdr_app;
