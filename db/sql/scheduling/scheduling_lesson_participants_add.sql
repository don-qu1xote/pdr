-- @no-dto
-- ПАЧКОЙ, А НЕ ПО СТРОКЕ В ЦИКЛЕ. Участников у занятия будет двадцать
-- (PDR-SCHED-08), и построчная вставка платит круговым ходом за каждого.
--
-- unnest разворачивает три массива в строки; массивы приезжают колонками —
-- ExecuteDecomposeBulk сам раскладывает по ним структуру
-- (docs/architecture/queries.md, «Вставка пачкой»).
INSERT INTO scheduling_lesson_participant (tenant_id, lesson_id, participant_id)
     SELECT *
       FROM unnest($1::uuid[], $2::uuid[], $3::uuid[])
