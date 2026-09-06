-- Послабления окон бронирования: что разрешено конкретному ученику.
--
-- Сами окна лежат не здесь. Умолчания практики приходят из динамического
-- конфига (PDR_BOOKING_WINDOWS, PDR_SCHEDULE_HORIZON) и действуют без единой
-- настройки: человек попадает в настройки, только когда сам захочет. В этой
-- таблице живёт исключение — «этому ученику можно свободнее», — а не правило.
--
-- НА СВЯЗКЕ, А НЕ У ЧЕЛОВЕКА. «Ему можно записаться за час» — утверждение про
-- отношения репетитора и ученика, а не про кого-то одного: у того же ученика с
-- другим репетитором прав нет никаких. Отсюда и первичный ключ из трёх колонок.
--
-- ТОЛЬКО ПОСЛАБЛЕНИЕ. Что строка не ужесточает окна, решает домен
-- (`scheduling::Relax`) до вставки, а не проверка в схеме: база не знает
-- умолчаний практики, они лежат в конфиге и меняются без миграции.
--
-- Владение таблицей — docs/architecture/context-map.md, контекст scheduling.

create table scheduling_booking_window (
    tenant_id  uuid not null references identity_tenant (tenant_id),
    tutor_id   uuid not null,
    student_id uuid not null,
    -- NULL здесь — «без ограничения», и это не то же, что ноль. Ноль значит
    -- «можно вплоть до самого начала» — решение; NULL значит «про это окно с
    -- этим учеником не договаривались вовсе».
    --
    -- Минуты, а не часы: домен меряет окна минутами, и перевод в часах терял бы
    -- «за сорок минут», которого репетитор вправе захотеть.
    book_before_minutes       integer,
    horizon_minutes           integer,
    reschedule_before_minutes integer,
    cancel_before_minutes     integer,
    -- Кто разрешил и когда. «Мне разрешили» через полгода превращается в спор,
    -- и разрешает его строка, а не память участников.
    granted_by uuid        not null,
    granted_at timestamptz not null default now(),
    constraint scheduling_booking_window_pk primary key (tenant_id, tutor_id, student_id),
    constraint scheduling_booking_window_tutor
        foreign key (tenant_id, tutor_id) references identity_person (tenant_id, id),
    constraint scheduling_booking_window_student
        foreign key (tenant_id, student_id) references identity_person (tenant_id, id),
    constraint scheduling_booking_window_granted_by
        foreign key (tenant_id, granted_by) references identity_person (tenant_id, id),
    constraint scheduling_booking_window_book_not_negative
        check (book_before_minutes is null or book_before_minutes >= 0),
    constraint scheduling_booking_window_horizon_not_negative
        check (horizon_minutes is null or horizon_minutes >= 0),
    constraint scheduling_booking_window_reschedule_not_negative
        check (reschedule_before_minutes is null or reschedule_before_minutes >= 0),
    constraint scheduling_booking_window_cancel_not_negative
        check (cancel_before_minutes is null or cancel_before_minutes >= 0),
    -- Строка, в которой не ослаблено ни одно окно, ничего не значит и только
    -- сбивает с толку того, кто её потом читает.
    constraint scheduling_booking_window_says_something
        check (num_nonnulls(book_before_minutes, horizon_minutes,
                            reschedule_before_minutes, cancel_before_minutes) > 0)
);
comment on table scheduling_booking_window is 'Послабление окон бронирования для одной пары репетитор-ученик. NULL в колонке окна — без ограничения. Умолчания практики лежат в динамическом конфиге, а не здесь.';

-- Своего индекса нет и не нужно: единственный запрос спрашивает про одну пару,
-- и первичный ключ (tenant_id, tutor_id, student_id) отвечает на него целиком.

grant select, insert, update, delete on scheduling_booking_window to pdr_app;

alter table scheduling_booking_window enable row level security;
alter table scheduling_booking_window force row level security;
create policy scheduling_booking_window_isolation on scheduling_booking_window
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
