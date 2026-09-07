-- Перерыв: отпуск, болезнь, каникулы — ОДНОЙ ТАБЛИЦЕЙ И ОДНИМ МЕХАНИЗМОМ.
--
-- «Репетитор в отпуске» и «ученик на каникулах» — это одно и то же с двух
-- сторон, а не две функции. Отсюда и колонка: перерыв у ЧЕЛОВЕКА, а не у
-- репетитора. Какой стороной он стоит в занятии, видно из самого занятия, и
-- второй таблицы «каникулы» рядом не появится.
--
-- ЭТО НЕ scheduling_availability_exception. Та таблица говорит, КОГДА
-- репетитор готов работать, и живёт по дням: «в эту субботу с двух до
-- четырёх». Здесь — период целиком, у него есть решение по занятиям, причина и
-- тот, кто его завёл. Выражать двухнедельный отпуск четырнадцатью строками
-- исключений значило бы потерять сам период: отменить по нему занятия одним
-- решением стало бы не по чему, а напомнить о выходе — тем более.
--
-- ЗАДНИМ ЧИСЛОМ РАЗРЕШЕНО, И ПРОВЕРКИ ПРОТИВ ЭТОГО ЗДЕСЬ НЕТ НАМЕРЕННО. Период
-- заводят ровно тогда, когда он уже идёт: человек заболел в понедельник и
-- дошёл до телефона в среду. `check (from_date >= current_date)` сделал бы
-- продукт бесполезным в тот единственный момент, когда он нужен.
--
-- Владение таблицей — docs/architecture/context-map.md, контекст scheduling.

create table scheduling_time_off (
    tenant_id uuid not null references identity_tenant (tenant_id),
    id        uuid not null,
    -- Кого не будет. Не «репетитор» и не «ученик»: у перерыва одна сторона.
    person_id uuid not null,
    -- ГРАНИЦЫ — МЕСТНЫЕ ДАТЫ, ОБЕ ВКЛЮЧИТЕЛЬНО. «С первого по четырнадцатое» —
    -- это то, что человек говорит, и хранить это надо тем же. Момент отсюда
    -- считает база (`date::timestamp at time zone tz`): таблица переводов часов
    -- есть у неё и нет у ядра, и это разделение намеренное.
    from_date date not null,
    to_date   date not null,
    -- Зона, в которой человек НАЗВАЛ даты. Рядом с датами по тому же правилу,
    -- по которому зона лежит рядом с моментом занятия (PDR-DB-01).
    tz        text not null,
    -- ПРИЧИНА НЕОБЯЗАТЕЛЬНА, И NULL ЗДЕСЬ — ОБЫЧНОЕ ДЕЛО, а не недозаполненная
    -- строка. Обязательная причина отсутствия — это объяснительная: человек,
    -- которому нечего в неё написать, либо не заведёт период вовсе, либо
    -- напишет что попало.
    --
    -- КАТЕГОРИЯ, А НЕ ТЕКСТ. «Болезнь» в свободном поле — это диагноз в базе:
    -- сведения о здоровье, которых мы не собираем (docs/legal/personal-data.md).
    reason    text,
    -- ЧТО РЕШИЛИ С ЗАНЯТИЯМИ И С СЕРИЯМИ. NULL — решение ещё не принято: период
    -- заводится первым действием, решение приходит вторым, и между ними человек
    -- смотрит список занятий.
    --
    -- Хранится потому, что по самим занятиям через полгода не восстанавливается:
    -- отменённое по отпуску и отменённое поштучно выглядят одинаково, а
    -- спрашивают о них по-разному.
    lessons_decided text,
    series_decided  text,
    decided_at      timestamptz,
    declared_by uuid        not null,
    declared_at timestamptz not null default now(),
    constraint scheduling_time_off_pk primary key (tenant_id, id),
    constraint scheduling_time_off_person
        foreign key (tenant_id, person_id) references identity_person (tenant_id, id),
    constraint scheduling_time_off_declared_by
        foreign key (tenant_id, declared_by) references identity_person (tenant_id, id),
    constraint scheduling_time_off_forward check (to_date >= from_date),
    constraint scheduling_time_off_tz_named check (length(btrim(tz)) > 0),
    constraint scheduling_time_off_reason_known
        check (reason is null or reason in ('vacation', 'sickness', 'break')),
    constraint scheduling_time_off_lessons_decision_known
        check (lessons_decided is null or lessons_decided in ('cancel', 'postpone', 'keep')),
    constraint scheduling_time_off_series_decision_known
        check (series_decided is null or series_decided in ('skip', 'shift')),
    -- Решение принимают целиком: «занятия отменили, а про серии не сказали» —
    -- это половина ответа, и половину эту потом никто не найдёт.
    constraint scheduling_time_off_decided_whole
        check (num_nonnulls(lessons_decided, series_decided, decided_at) in (0, 3)),
    -- ДВА ПЕРЕРЫВА У ОДНОГО ЧЕЛОВЕКА НЕ НАКЛАДЫВАЮТСЯ.
    --
    -- «Я в отпуске и одновременно болею» — это один период, а не два: решение
    -- по занятиям у него одно, и два наложенных периода означали бы два решения
    -- на одно и то же занятие. Ловит это база, а не сценарий: между чтением и
    -- записью помещается второе обращение, и доменная проверка его не увидит.
    constraint scheduling_time_off_no_overlap exclude using gist (
        tenant_id with =,
        person_id with =,
        daterange(from_date, to_date, '[]') with &&
    )
);
comment on table scheduling_time_off is 'Перерыв человека: отпуск, болезнь, каникулы. Границы — местные даты включительно, причина необязательна, решение по занятиям приходит вторым действием.';
comment on column scheduling_time_off.reason is 'Категория, а не описание: диагнозов мы не храним. NULL — обычное дело, причина необязательна.';
comment on column scheduling_time_off.lessons_decided is 'ОДНО решение на все занятия периода. NULL — период заведён, решение ещё не принято.';

-- ПОД ОДИН НАСТОЯЩИЙ ЗАПРОС: серии человека, в которых он участник.
--
-- У занятий такой индекс уже есть (`scheduling_lesson_by_participant`), у серий
-- не было: до перерывов серию спрашивали только по её идентификатору. Каникулы
-- ученика спрашивают иначе — «какие серии его касаются», — и без индекса это
-- перебор всей таблицы участий.
create index scheduling_series_by_participant
    on scheduling_series_participant (tenant_id, participant_id, series_id);

grant select, insert, update on scheduling_time_off to pdr_app;

alter table scheduling_time_off enable row level security;
alter table scheduling_time_off force row level security;
create policy scheduling_time_off_isolation on scheduling_time_off
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
