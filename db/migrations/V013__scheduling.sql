-- Расписание: доступность, занятия, серии.
--
-- Первое настоящее применение слоёв. Домен календаря написан и проверен без
-- базы (PDR-SCHED-01, PDR-SCHED-02) — здесь к нему приделывается хранение, и
-- домен об этом не узнаёт: ни одного его заголовка эта миграция не меняет.

-- РАСШИРЕНИЕ ДЛЯ ОГРАНИЧЕНИЯ НЕПЕРЕСЕЧЕНИЯ.
--
-- `exclude using gist` ниже сравнивает uuid на равенство и отрезки времени на
-- пересечение в одном ограничении. Классы операторов для uuid в gist даёт
-- btree_gist; без него ограничение не создаётся вовсе.
create extension if not exists btree_gist;

-- ВРЕМЯ НА ЧАСАХ ХРАНИТСЯ МИНУТАМИ ОТ ПОЛУНОЧИ, А НЕ ТИПОМ time.
--
-- `time` без зоны в этом дереве не встречается нигде (docs/architecture/migrations.md):
-- «18:00» без зоны — надпись, а не момент. Пара «минуты от полуночи + tz» —
-- это ровно то, чем она и является: показание часов, которое станет моментом
-- только вместе с зоной, и станет им по правилам той зоны (core::Resolve).
-- Отдельный тип для показания часов Postgres не предлагает, а `time`
-- притворяется, что предлагает.

-- ДОСТУПНОСТЬ: НЕДЕЛЬНОЕ ПРАВИЛО.
--
-- День недели числом 0..6 с воскресеньем в нуле — та же нумерация, что у
-- `core::Weekday` и у `std::chrono::weekday`. Своей нумерации не заводим: две
-- нумерации одного и того же расходятся ровно в тот день, когда одну из них
-- поправили.
create table scheduling_availability (
    tenant_id        uuid        not null,
    id               uuid        not null,
    tutor_id         uuid        not null,
    weekday          smallint    not null,
    from_minute      smallint    not null,
    to_minute        smallint    not null,
    tz               text        not null,
    created_at       timestamptz not null default now(),
    constraint scheduling_availability_pk primary key (tenant_id, id),
    constraint scheduling_availability_weekday_known check (weekday between 0 and 6),
    constraint scheduling_availability_from_on_the_clock check (from_minute between 0 and 1439),
    constraint scheduling_availability_to_on_the_clock check (to_minute between 1 and 1440),
    constraint scheduling_availability_forward check (to_minute > from_minute),
    constraint scheduling_availability_tz_named check (length(btrim(tz)) > 0)
);
comment on table scheduling_availability is 'Когда репетитор готов работать: день недели и часы ПО ЕГО ЧАСАМ. Зона рядом, потому что «с десяти» — это утверждение про его часы.';

-- ИСКЛЮЧЕНИЕ ИЗ ПРАВИЛА — ОТДЕЛЬНОЙ ТАБЛИЦЕЙ И ДВУХ ВИДОВ СРАЗУ.
--
-- Пустой отрезок означает выходной. Это не «забыли заполнить»: «в этот день не
-- работаю» и «в этот день работаю с двух до четырёх» — разные ответы, и первый
-- обязан выражаться. Отсюда и check: либо обе границы, либо ни одной.
create table scheduling_availability_exception (
    tenant_id    uuid        not null,
    tutor_id     uuid        not null,
    on_date      date        not null,
    starts_at    timestamptz,
    ends_at      timestamptz,
    created_at   timestamptz not null default now(),
    constraint scheduling_availability_exception_pk primary key (tenant_id, tutor_id, on_date),
    constraint scheduling_availability_exception_whole
        check ((starts_at is null) = (ends_at is null)),
    constraint scheduling_availability_exception_forward
        check (ends_at is null or ends_at > starts_at)
);
comment on table scheduling_availability_exception is 'День, который живёт не по правилу: выходной (без часов) или иные часы. Сильнее недельного правила.';

-- ЗАНЯТИЕ.
--
-- Хранятся ДВА момента, а не момент и длительность, и это не избыточность.
-- Ограничение непересечения ниже строится на `tstzrange(starts_at, ends_at)`, а
-- индексное выражение обязано быть неизменяемым: `timestamptz + interval` в
-- Postgres помечен stable, и построить по нему индекс нельзя. Длительность из
-- пары моментов получается вычитанием и в домен возвращается ею же.
--
-- Зона рядом с моментом — правило PDR-DB-01: занятие в 18:00 по Берлину,
-- перенесённое государством на час, обязано остаться в 18:00 по Берлину, и без
-- зоны рядом это невыразимо.
create table scheduling_lesson (
    tenant_id   uuid        not null,
    id          uuid        not null,
    series_id   uuid,
    tutor_id    uuid        not null,
    starts_at   timestamptz not null,
    ends_at     timestamptz not null,
    tz          text        not null,
    state       text        not null,
    created_at  timestamptz not null default now(),
    constraint scheduling_lesson_pk primary key (tenant_id, id),
    constraint scheduling_lesson_forward check (ends_at > starts_at),
    constraint scheduling_lesson_tz_named check (length(btrim(tz)) > 0),
    constraint scheduling_lesson_state_known
        check (state in ('planned', 'confirmed', 'held', 'cancelled', 'no_show')),
    -- ЗАЩИТА ОТ ГОНКИ — В БАЗЕ, А НЕ ТОЛЬКО В ДОМЕНЕ.
    --
    -- Доменная проверка пересечений есть (`scheduling::Overlaps`), и она нужна:
    -- она отвечает человеку словами и до всякого обращения к базе. Но она
    -- смотрит на то, что прочитала, а между чтением и записью помещается второе
    -- бронирование. Два параллельных запроса на пересекающийся слот проходят
    -- доменную проверку оба и оба пишут — если писать некому помешать.
    --
    -- Это не дублирование, а два уровня: домен объясняет, база гарантирует.
    -- Второй пишущий получает отказ от самой базы, и получает его всегда, каким
    -- бы ни было чередование.
    --
    -- Отменённые и не состоявшиеся занятия из-под ограничения выведены: слот,
    -- освободившийся после отмены, обязан снова стать доступным.
    constraint scheduling_lesson_no_overlap exclude using gist (
        tenant_id with =,
        tutor_id with =,
        tstzrange(starts_at, ends_at) with &&
    ) where (state in ('planned', 'confirmed'))
);
comment on table scheduling_lesson is 'Занятие: два момента в UTC, зона задумки рядом и состояние из закрытого списка. Пересечения у репетитора запрещены самой базой.';

-- УЧАСТНИКИ ОТДЕЛЬНОЙ ТАБЛИЦЕЙ — СЛЕДСТВИЕ ВЕКТОРА В ДОМЕНЕ.
--
-- В `scheduling::Lesson` участник — вектор, потому что групповые занятия
-- появятся (PDR-SCHED-08). Колонка `participant_id` в самом занятии заставила
-- бы переписывать схему, запросы и индексы в тот день; отдельная таблица не
-- заставит. Сегодняшнее правило «участник ровно один» живёт в домене, а не в
-- схеме: правило меняется строкой, форма таблицы — миграцией.
create table scheduling_lesson_participant (
    tenant_id      uuid        not null,
    lesson_id      uuid        not null,
    participant_id uuid        not null,
    constraint scheduling_lesson_participant_pk
        primary key (tenant_id, lesson_id, participant_id),
    constraint scheduling_lesson_participant_lesson
        foreign key (tenant_id, lesson_id)
        references scheduling_lesson (tenant_id, id) on delete cascade
);
comment on table scheduling_lesson_participant is 'Кто занимается на этом занятии. Отдельная таблица, потому что участник в домене — вектор.';

-- СЕРИЯ ХРАНИТСЯ ПРАВИЛОМ, А НЕ РАЗВЁРНУТЫМ СПИСКОМ.
--
-- Сорок строк, созданных при заведении серии, ломаются на первом же переносе:
-- правило и список расходятся, и починить их можно только руками. Здесь лежит
-- само правило текстом RRULE — тем же подмножеством RFC 5545, которое разбирает
-- `scheduling::RecurrenceRule`, — а занятия считаются по запросу.
--
-- `at_minute` и `tz` вместе значат «в 18:00 по часам репетитора». В UTC время
-- серии не хранится: серия, хранящая UTC, дважды в год уезжает на час.
create table scheduling_series (
    tenant_id        uuid        not null,
    id               uuid        not null,
    tutor_id         uuid        not null,
    rrule            text        not null,
    starts_on        date        not null,
    at_minute        smallint    not null,
    tz               text        not null,
    duration_minutes integer     not null,
    created_at       timestamptz not null default now(),
    constraint scheduling_series_pk primary key (tenant_id, id),
    constraint scheduling_series_rrule_written check (length(btrim(rrule)) > 0),
    constraint scheduling_series_at_on_the_clock check (at_minute between 0 and 1439),
    constraint scheduling_series_tz_named check (length(btrim(tz)) > 0),
    constraint scheduling_series_duration_positive check (duration_minutes > 0)
);
comment on table scheduling_series is 'Регулярные занятия правилом RRULE, а не списком. Время задано по часам репетитора: минуты от полуночи плюс зона.';

create table scheduling_series_participant (
    tenant_id      uuid not null,
    series_id      uuid not null,
    participant_id uuid not null,
    constraint scheduling_series_participant_pk
        primary key (tenant_id, series_id, participant_id),
    constraint scheduling_series_participant_series
        foreign key (tenant_id, series_id)
        references scheduling_series (tenant_id, id) on delete cascade
);
comment on table scheduling_series_participant is 'Кто занимается по этой серии. Отдельная таблица по той же причине, что и у занятия.';

-- ИСКЛЮЧЕНИЕ ИЗ СЕРИИ — ДВУХ ВИДОВ, А НЕ ОДНОГО.
--
-- «Отменили» и «перенесли» — разные события и для расписания, и для оплаты, и
-- для ученика. Один вид заставил бы изображать перенос парой «отмена плюс
-- новое занятие», и связь между ними держалась бы на памяти того, кто её завёл.
--
-- Вхождение опознаётся МЕСТНОЙ ДАТОЙ, на которую его ставит правило: момент у
-- вхождения меняется при переводе часов, а дата — нет.
create table scheduling_series_exception (
    tenant_id        uuid        not null,
    series_id        uuid        not null,
    occurrence_on    date        not null,
    kind             text        not null,
    moved_to         timestamptz,
    moved_minutes    integer,
    created_at       timestamptz not null default now(),
    constraint scheduling_series_exception_pk primary key (tenant_id, series_id, occurrence_on),
    constraint scheduling_series_exception_series
        foreign key (tenant_id, series_id)
        references scheduling_series (tenant_id, id) on delete cascade,
    constraint scheduling_series_exception_kind_known check (kind in ('cancelled', 'moved')),
    -- Перенос без нового времени — это отмена, и называться должен так же;
    -- у отмены нового времени не бывает. Схема говорит то же, что домен.
    constraint scheduling_series_exception_place_matches_kind
        check ((kind = 'moved') = (moved_to is not null)),
    constraint scheduling_series_exception_moved_length
        check (moved_minutes is null or moved_minutes > 0),
    constraint scheduling_series_exception_length_needs_place
        check (moved_minutes is null or moved_to is not null)
);
comment on table scheduling_series_exception is 'Отменённое или перенесённое вхождение серии. Опознаётся местной датой: момент при переводе часов меняется, дата — нет.';

-- ИНДЕКСЫ ПОД ДВА НАСТОЯЩИХ ЗАПРОСА, А НЕ ПОД ГИПОТЕТИЧЕСКИЕ.
--
-- Запроса ровно два, и оба названы портом (`ports::LessonRepository`): занятия
-- репетитора за диапазон и занятия участника за диапазон. Третьего индекса
-- здесь нет намеренно: индекс, заведённый «на всякий случай», платится записью
-- на каждой вставке и не платит ничем.
create index scheduling_lesson_by_tutor on scheduling_lesson (tenant_id, tutor_id, starts_at);
create index scheduling_lesson_by_participant
    on scheduling_lesson_participant (tenant_id, participant_id, lesson_id);
create index scheduling_availability_by_tutor on scheduling_availability (tenant_id, tutor_id);
create index scheduling_series_by_tutor on scheduling_series (tenant_id, tutor_id);

grant select, insert, update, delete on scheduling_availability to pdr_app;
grant select, insert, update, delete on scheduling_availability_exception to pdr_app;
grant select, insert, update on scheduling_lesson to pdr_app;
grant select, insert, delete on scheduling_lesson_participant to pdr_app;
grant select, insert, update, delete on scheduling_series to pdr_app;
grant select, insert, delete on scheduling_series_participant to pdr_app;
grant select, insert, update, delete on scheduling_series_exception to pdr_app;

alter table scheduling_availability enable row level security;
alter table scheduling_availability force row level security;
create policy scheduling_availability_isolation on scheduling_availability
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_availability_exception enable row level security;
alter table scheduling_availability_exception force row level security;
create policy scheduling_availability_exception_isolation on scheduling_availability_exception
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_lesson enable row level security;
alter table scheduling_lesson force row level security;
create policy scheduling_lesson_isolation on scheduling_lesson
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_lesson_participant enable row level security;
alter table scheduling_lesson_participant force row level security;
create policy scheduling_lesson_participant_isolation on scheduling_lesson_participant
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_series enable row level security;
alter table scheduling_series force row level security;
create policy scheduling_series_isolation on scheduling_series
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_series_participant enable row level security;
alter table scheduling_series_participant force row level security;
create policy scheduling_series_participant_isolation on scheduling_series_participant
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);

alter table scheduling_series_exception enable row level security;
alter table scheduling_series_exception force row level security;
create policy scheduling_series_exception_isolation on scheduling_series_exception
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
