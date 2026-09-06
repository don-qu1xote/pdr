-- @no-dto
-- ПАЧКОЙ, А НЕ ПО СТРОКЕ В ЦИКЛЕ. Участников у занятия будет двадцать
-- (PDR-SCHED-08), и построчная вставка платит круговым ходом за каждого.
--
-- unnest разворачивает массивы в строки; массивы приезжают колонками —
-- ExecuteDecomposeBulk сам раскладывает по ним структуру
-- (docs/architecture/queries.md, «Вставка пачкой»).
--
-- Цена, оплата и исход едут отсюда же: они у каждого участия СВОИ, и вставлять
-- их вторым запросом значило бы оставить промежуток, в котором участие уже есть,
-- а сколько оно стоит — ещё неизвестно.
INSERT INTO scheduling_lesson_participant
    (tenant_id, lesson_id, participant_id, price_minor, currency, payment, attendance, state)
     SELECT *
       FROM unnest($1::uuid[], $2::uuid[], $3::uuid[], $4::bigint[], $5::char(3)[],
                   $6::text[], $7::text[], $8::text[])
