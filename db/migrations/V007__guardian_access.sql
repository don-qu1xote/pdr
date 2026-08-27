-- Родительский доступ: по уровню за раз, с датой отзыва и со сроком на решение.
--
-- Стоит ДО расписания и денег намеренно. Доступ, прошитый через готовые
-- подсистемы задним числом, — самая дорогая переделка: он трогает каждое
-- чтение в каждом контексте, и половину мест находят по жалобам.
--
-- Правила целиком — docs/architecture/permissions.md, что делать по запросу
-- на отзыв — docs/runbooks/guardian-access.md.

-- ДАТА РОЖДЕНИЯ. Без неё правило совершеннолетия не на что опереть: возраст
-- считается от даты и момента, а не хранится числом, которое врёт со
-- следующего дня рождения (identity::AgeStatus).
--
-- Колонка допускает пустоту: люди, заведённые раньше этой миграции, её не
-- называли. Пустая дата означает, что правило совершеннолетия НЕ СРАБАТЫВАЕТ —
-- отобрать доступ у родителя из-за незаполненной колонки хуже, чем оставить
-- его до выяснения. Что делать с такими — в руководстве выше.
--
-- date, а не timestamptz: день рождения — это дата в календаре человека, а не
-- момент на оси. Момент совершеннолетия из неё считает домен.
alter table identity_person add column born_on date;
comment on column identity_person.born_on is 'День рождения. Пусто у людей, заведённых до PDR-IDENT-04: для них правило совершеннолетия не срабатывает.';

-- ЧЕМ КОНЧИЛАСЬ ПОПЫТКА ПОСМОТРЕТЬ. Отказ попадает в журнал наравне с
-- просмотром: «кто-то пытался открыть твою запись занятия, и ему не дали» —
-- сведение, которое ученику нужнее списка удачных просмотров.
--
-- Умолчание 'shown' нужно только старым строкам: до этой миграции в журнал
-- писали лишь состоявшиеся просмотры, и переписывать их задним числом нечем.
alter table identity_access_log add column outcome text not null default 'shown';
alter table identity_access_log add constraint identity_access_log_outcome_known
    check (outcome in ('shown', 'refused'));
comment on column identity_access_log.outcome is 'Показали или отказали. Отказ в журнале — не шум, а то, что человеку важнее всего.';

-- Согласие на ОДИН уровень доступа опекуна.
--
-- СТРОКА НА УРОВЕНЬ, а не флаги в одной. Родитель, которому нужно платить, и
-- родитель, которому нужно слушать записи уроков, — два разных решения; в одной
-- строке они превращаются в «включи, чтобы работало», и включают всё.
--
-- ОТЗЫВ — ЭТО ДАТА, А НЕ УДАЛЕНИЕ. По выданному доступу человек смотрел чужие
-- данные, и на вопрос «кто имел доступ в марте» отвечает эта таблица вместе с
-- журналом. Удалённая строка отвечает «никто», и это неправда.
--
-- granted_by не украшение: от него зависит поведение при совершеннолетии.
-- Согласие, которое дал сам ученик, подтверждения больше не требует — своё
-- слово он уже сказал (identity::WeighConsents).
create table identity_guardian_consent (
    tenant_id   uuid        not null references identity_tenant (tenant_id),
    id          uuid        not null,
    guardian_id uuid        not null,
    student_id  uuid        not null,
    scope       text        not null,
    granted_at  timestamptz not null default now(),
    granted_by  uuid        not null,
    expires_at  timestamptz,
    revoked_at  timestamptz,
    revoked_by  uuid,
    constraint identity_guardian_consent_pk primary key (tenant_id, id),
    constraint identity_guardian_consent_guardian_fk
        foreign key (tenant_id, guardian_id) references identity_person (tenant_id, id),
    constraint identity_guardian_consent_student_fk
        foreign key (tenant_id, student_id) references identity_person (tenant_id, id),
    constraint identity_guardian_consent_granted_by_fk
        foreign key (tenant_id, granted_by) references identity_person (tenant_id, id),
    constraint identity_guardian_consent_revoked_by_fk
        foreign key (tenant_id, revoked_by) references identity_person (tenant_id, id),
    constraint identity_guardian_consent_not_self check (guardian_id <> student_id),
    -- Список уровней закрыт и повторяет identity::GuardianScope. Единого
    -- «родитель видит всё» здесь нет и завести его нельзя, не тронув эту строку.
    constraint identity_guardian_consent_scope_known
        check (scope in ('schedule', 'payments', 'notes_and_homework', 'recordings')),
    constraint identity_guardian_consent_expires_after_granted
        check (expires_at is null or expires_at > granted_at),
    constraint identity_guardian_consent_revoked_after_granted
        check (revoked_at is null or revoked_at >= granted_at),
    -- Отзыв всегда чей-то: «доступ закрылся сам» — это не отзыв, а истечение
    -- срока, и оно живёт в expires_at.
    constraint identity_guardian_consent_revoked_by_someone
        check ((revoked_at is null) = (revoked_by is null))
);
comment on table identity_guardian_consent is 'Согласие на один уровень доступа опекуна. Отзыв — строка с датой: журнал обязан отвечать на «кто имел доступ в марте».';

-- Действующее согласие на пару и уровень одно. Отозванных сколько угодно:
-- открыли, отозвали, открыли снова — обычная история одной семьи.
--
-- Он же отвечает на главный вопрос к таблице — «что открыто ЭТОМУ опекуну про
-- ЭТОГО ученика», который задаётся на каждом обращении опекуна: первые три
-- колонки и есть эта пара, а условие частичности совпадает дословно. Второго
-- индекса под тот же вопрос здесь нет намеренно — он стоил бы записи и места,
-- а план от него не менялся бы (db/explain/hot_queries.sql).
create unique index identity_guardian_consent_active
    on identity_guardian_consent (tenant_id, guardian_id, student_id, scope)
    where revoked_at is null;

alter table identity_guardian_consent enable row level security;
alter table identity_guardian_consent force row level security;

create policy identity_guardian_consent_isolation on identity_guardian_consent
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Права роли приложения. delete не выдаётся: отозванное согласие остаётся
-- строкой с датой, иначе «доступ был отозван в марте» и «доступа никогда не
-- было» перестают различаться.
grant select, insert, update on identity_guardian_consent to pdr_app;
