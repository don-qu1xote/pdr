-- Согласия на обработку персональных данных и на запись занятий.
--
-- СОГЛАСИЕ ХРАНИТСЯ, А НЕ ПОДРАЗУМЕВАЕТСЯ. «Он же зарегистрировался, значит
-- согласился» — это утверждение, которое нечем подтвердить, и спрашивают его
-- ровно тогда, когда ответить уже нечем. Здесь записано пять вещей: о ком, кто
-- дал, на что, какую версию перечня и каким действием.
--
-- ДВЕ ГАЛОЧКИ, А НЕ ОДНА, и это видно в схеме: kind — часть уникальности.
-- Правовой вес у них разный. Без согласия на обработку продукта нет вовсе; без
-- согласия на запись продукт работает, просто конспекта из записи не будет.
-- Одна строка на оба вида означала бы, что человек, которому не нужна запись,
-- отказывается от продукта целиком.
--
-- Перечень, на который даётся согласие, — docs/legal/personal-data.md.
-- Владение таблицами — docs/architecture/context-map.md (контекст identity).
create table identity_consent (
    tenant_id    uuid        not null,
    id           uuid        not null,
    -- О ком согласие: ученик, в том числе ребёнок.
    subject_id   uuid        not null,
    -- Кто его дал: сам ученик или его опекун. За ребёнка соглашается опекун, и
    -- различить одно от другого потом можно только по этой паре колонок.
    given_by     uuid        not null,
    kind         text        not null,
    -- Версия перечня. Без неё строка не отвечает на вопрос «на что именно
    -- человек согласился», а перечень меняется.
    version      integer     not null,
    -- Каким действием. Галочка при регистрации и подтверждение новой версии в
    -- кабинете — разные действия с разной судьбой.
    action       text        not null,
    given_at     timestamptz not null default now(),
    -- Отзыв — строка с датой, а не удаление: на вопрос «а было ли согласие в
    -- марте» отвечает эта строка, а удалённая отвечает «нет», и это неправда.
    withdrawn_at timestamptz,
    constraint identity_consent_pk primary key (tenant_id, id),
    constraint identity_consent_subject_fk
        foreign key (tenant_id, subject_id) references identity_person (tenant_id, id),
    constraint identity_consent_given_by_fk
        foreign key (tenant_id, given_by) references identity_person (tenant_id, id),
    constraint identity_consent_kind_known check (kind in ('processing', 'recordings')),
    constraint identity_consent_action_known
        check (action in ('sign_up_checkbox', 'settings_checkbox', 'version_accepted')),
    constraint identity_consent_version_from_one check (version >= 1),
    constraint identity_consent_withdrawn_after_given
        check (withdrawn_at is null or withdrawn_at >= given_at)
);
comment on table identity_consent is 'Согласие на обработку по перечню и на запись занятий: кто дал, когда, какую версию и каким действием. За ребёнка соглашается опекун.';
comment on column identity_consent.version is 'Версия перечня docs/legal/personal-data.md, принятая этим согласием. Существенное изменение требует нового согласия, косметическое — нет.';
comment on column identity_consent.given_by is 'Кто дал: сам ученик или его опекун. Совпадает с subject_id — значит, человек согласился сам.';

-- Действующее согласие на пару «человек и вид» ровно одно: второе означало бы,
-- что на вопрос «а согласен ли он на запись» есть два ответа. Частичность по
-- withdrawn_at оставляет историю: отозванных строк может быть сколько угодно.
create unique index identity_consent_live
    on identity_consent (tenant_id, subject_id, kind)
    where withdrawn_at is null;

-- Экран «мои данные» спрашивает согласия одного человека и показывает их
-- вместе с отозванными: этот индекс и отвечает.
create index identity_consent_by_subject
    on identity_consent (tenant_id, subject_id, given_at desc);

alter table identity_consent enable row level security;
alter table identity_consent force row level security;

create policy identity_consent_isolation on identity_consent
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

-- Прав delete нет: отозванное согласие остаётся строкой с датой. Приложение,
-- умеющее удалить согласие, умеет и стереть след того, что оно было.
grant select, insert, update on identity_consent to pdr_app;
