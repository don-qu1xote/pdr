-- Участие вместо участника: у каждого своя цена, своя оплата и свой исход.
--
-- ГРУППОВЫЕ ЗАНЯТИЯ ЗАКЛАДЫВАЮТСЯ В МОДЕЛЬ, А НЕ ВВОДЯТСЯ. Функция отложена
-- решением владельца, но форма данных готовится сейчас: занятие с колонкой
-- «состояние оплаты» пришлось бы переписывать целиком в тот день, когда
-- участников станет двое, — второе по дороговизне переписывание после
-- тенантности (docs/adr/0023-group-lessons-modelled-not-implemented.md).
--
-- Сама таблица участий заведена ещё в V013 и уже была отдельной: сегодня она
-- обрастает тем, что у каждого участника СВОЁ.
--
-- Правило «участник ровно один» по-прежнему живёт в домене, а не здесь: правило
-- меняется строкой, ограничение схемы — миграцией. Схема готова к нескольким
-- участиям с самого начала, и менять её в день групп не придётся.

-- ЦЕНА НА УЧАСТИИ, А НЕ НА ЗАНЯТИИ. В группе один платит по своему тарифу,
-- второй по своему, а занятие одно: общей цены у него не бывает.
--
-- NULL значит «ещё не назначена», а не «бесплатно»: бесплатное участие — это
-- назначенный ноль. Назначает цену не расписание, она приходит извне, и на
-- момент записи её может ещё не быть.
alter table scheduling_lesson_participant add column price_minor bigint;
alter table scheduling_lesson_participant add column currency char(3);
alter table scheduling_lesson_participant add constraint
    scheduling_lesson_participant_price_whole
    check (num_nonnulls(price_minor, currency) <> 1);
alter table scheduling_lesson_participant add constraint
    scheduling_lesson_participant_currency_shaped
    check (currency is null or currency ~ '^[A-Z]{3}$');

-- ОПЛАТА ТОЖЕ НА УЧАСТИИ: в группе один заплатил, второй нет, и «оплачено ли
-- занятие» не выражается вовсе. Что случилось с деньгами дальше — возврат,
-- удержание, зачёт из пакета — знает биллинг, а не расписание.
alter table scheduling_lesson_participant
    add column payment text not null default 'unpaid';
alter table scheduling_lesson_participant add constraint
    scheduling_lesson_participant_payment_known
    check (payment in ('unpaid', 'paid'));

-- ИСХОД У КАЖДОГО СВОЙ. Занятие общее, а был на нём человек или нет — факт про
-- человека: в группе из троих один не пришёл, и занятие всё равно состоялось.
-- Прогресс читает именно это, а не состояние занятия.
alter table scheduling_lesson_participant
    add column attendance text not null default 'expected';
alter table scheduling_lesson_participant add constraint
    scheduling_lesson_participant_attendance_known
    check (attendance in ('expected', 'attended', 'missed'));

-- УЧАСТНИК ВЫШЕЛ ≠ ЗАНЯТИЕ ОТМЕНЕНО. Состояние занятия лежит в самом занятии и
-- касается всех сразу; это — про одного, и одно из другого не выводится.
alter table scheduling_lesson_participant
    add column state text not null default 'joined';
alter table scheduling_lesson_participant add constraint
    scheduling_lesson_participant_state_known
    check (state in ('joined', 'withdrawn'));

comment on column scheduling_lesson_participant.price_minor is 'Цена ЭТОГО участия. NULL — ещё не назначена, а не бесплатно: бесплатное участие это назначенный ноль.';
comment on column scheduling_lesson_participant.state is 'Вышел ли участник. Отмена занятия живёт в самом занятии и касается всех сразу.';

-- Права те же: колонки добавлены в таблицу, на которую они уже выданы.
