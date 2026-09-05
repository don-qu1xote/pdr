-- Кто занимается на этих занятиях. Одним обращением на список занятий, а не
-- запросом на занятие: месяц расписания это тридцать занятий и тридцать
-- круговых ходов, если спрашивать по одному.
SELECT lesson_id::text AS lesson_id,
       participant_id::text AS participant_id
  FROM scheduling_lesson_participant
 WHERE tenant_id = $1::uuid
   AND lesson_id = ANY($2::uuid[])
 ORDER BY lesson_id, participant_id
