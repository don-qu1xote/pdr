-- История изменений занятия.
--
-- НЕ АУДИТ РАДИ АУДИТА. Спор «я отменял заранее» возникает гарантированно, и
-- разрешает его не память участников, а строка с моментом: кто, что и когда
-- сделал с этим занятием. Без неё платформа в таком споре не свидетель, а
-- сторона, которой просто верят на слово.
--
-- ПИШЕТСЯ ТОЛЬКО ВПЕРЁД. Права на update и delete прикладной роли не выдаются
-- ниже: история, которую можно поправить, спор не решает, а переводит в спор о
-- самой истории.
create table scheduling_lesson_history (
    tenant_id  uuid        not null,
    id         uuid        not null,
    lesson_id  uuid        not null,
    actor_id   uuid        not null,
    action     text        not null,
    at         timestamptz not null,
    -- Подробности словами и в свободной форме: во сколько занятие стояло до
    -- переноса и прочее, что закрытым списком не выражается. РЕШЕНИЙ ПО НИМ НЕ
    -- ПРИНИМАЮТ — всё, на чём стоят правила, лежит в колонках рядом и в самом
    -- занятии. Текст, а не jsonb: разбирать это некому, читает человек.
    details    text        not null default '',
    created_at timestamptz not null default now(),
    constraint scheduling_lesson_history_pk primary key (tenant_id, id),
    constraint scheduling_lesson_history_lesson
        foreign key (tenant_id, lesson_id)
        references scheduling_lesson (tenant_id, id) on delete cascade,
    -- Список действий закрыт и повторяет scheduling::LessonAction. Свободная
    -- строка здесь означала бы, что через полгода в истории окажутся
    -- «отменено», «Отменено» и «отмена», а сравнивать их будет некому.
    constraint scheduling_lesson_history_action_known
        check (action in ('booked', 'confirmed', 'rescheduled',
                          'cancelled_by_student', 'cancelled_by_tutor',
                          'held', 'no_show'))
);
comment on table scheduling_lesson_history is 'Кто, что и когда сделал с занятием. Пишется только вперёд: правок и удалений у истории не бывает.';

-- ИНДЕКС ПОД ОДИН НАСТОЯЩИЙ ЗАПРОС: история одного занятия по возрастанию
-- момента. Второго запроса к этой таблице нет, и второго индекса тоже.
create index scheduling_lesson_history_by_lesson
    on scheduling_lesson_history (tenant_id, lesson_id, at);

grant select, insert on scheduling_lesson_history to pdr_app;

alter table scheduling_lesson_history enable row level security;
alter table scheduling_lesson_history force row level security;
create policy scheduling_lesson_history_isolation on scheduling_lesson_history
    using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
    with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid);
