-- Подписка на календарь: защищённая ссылка на ленту ICS.
--
-- НАПРАВЛЕНИЕ ОДНОСТОРОННЕЕ. Наши занятия появляются у человека в календаре;
-- его личные события к нам не переезжают, и таблицы под них здесь нет и не
-- будет. Чтение чужого календаря — это три OAuth-интеграции с чужими ключами,
-- квотами и правом сломаться (ADR-0024, ADR-0014).
--
-- В БАЗЕ САМОГО СЕКРЕТА НЕТ — только отпечаток SHA-256, как у одноразовой
-- ссылки identity. Утёкшая копия базы не даёт ни одной работающей ссылки:
-- секрет из отпечатка не восстанавливается, а сравнить пришедшее с хранимым он
-- позволяет.
--
-- ОДНА ПОДПИСКА НА ЧЕЛОВЕКА — первичным ключом, а не дисциплиной. Отозвать
-- ссылку обязано быть ОДНИМ действием: со списком из четырёх ссылок человек в
-- минуту утечки отзывает не ту. Перевыпуск — это та же строка с новым
-- отпечатком, и старая ссылка перестаёт работать в тот же миг.
--
-- АРЕНДАТОР СТОИТ В САМОМ АДРЕСЕ ЛЕНТЫ, и поэтому сквозной политики здесь нет
-- ни одной: поиск по отпечатку идёт уже внутри объявленного арендатора, как и
-- всё остальное. Тем же способом устроен вход (`/cabinet/{tenant}/sign-in`):
-- кабинет в адресе — не секрет, секрет — то, что рядом с ним.
--
-- Владение таблицей — docs/architecture/context-map.md, контекст scheduling.

create table scheduling_calendar_feed (
    tenant_id     uuid not null references identity_tenant (tenant_id),
    person_id     uuid not null,
    -- Отпечаток секрета ссылки: 64 строчных шестнадцатеричных знака.
    secret_digest char(64) not null,
    -- Показывать ли имена в названии события. Умолчание — не показывать:
    -- календарь открыт шире, чем кабинет, и имя ученика в чужом поле зрения мы
    -- не контролируем никак.
    naming        text not null default 'without_names',
    issued_at     timestamptz not null,
    constraint scheduling_calendar_feed_pk primary key (tenant_id, person_id),
    constraint scheduling_calendar_feed_person
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    constraint scheduling_calendar_feed_digest_shaped
        check (secret_digest ~ '^[0-9a-f]{64}$'),
    constraint scheduling_calendar_feed_naming_known
        check (naming in ('without_names', 'with_names'))
);
comment on table scheduling_calendar_feed is 'Подписка на ленту ICS: отпечаток секрета ссылки, а не сам секрет. Одна на человека — чтобы отзыв был одним действием.';
comment on column scheduling_calendar_feed.naming is 'Показывать ли имена в названии события. Умолчание — не показывать.';

-- ПОД ЕДИНСТВЕННЫЙ ЗАПРОС ЛЕНТЫ: найти подписку по отпечатку внутри кабинета.
--
-- Уникальный: два одинаковых отпечатка в одном кабинете означали бы, что один
-- секрет открывает два расписания, и который из них отдать — вопрос без ответа.
create unique index scheduling_calendar_feed_by_digest
    on scheduling_calendar_feed (tenant_id, secret_digest);

grant select, insert, update on scheduling_calendar_feed to pdr_app;

alter table scheduling_calendar_feed enable row level security;
alter table scheduling_calendar_feed force row level security;
create policy scheduling_calendar_feed_isolation on scheduling_calendar_feed
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
