-- Одно занятие по его идентификатору. Своего индекса не просит: пара
-- (tenant_id, id) — это первичный ключ scheduling_lesson_pk.
SELECT id::text AS id,
       tutor_id::text AS tutor_id,
       series_id::text AS series_id,
       starts_at,
       ends_at,
       tz,
       state
  FROM scheduling_lesson
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
