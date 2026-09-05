-- Занято ли у репетитора это время. Одна строка, а не диапазон: вопрос другой.
SELECT id::text AS id,
       tutor_id::text AS tutor_id,
       series_id::text AS series_id,
       starts_at,
       ends_at,
       tz,
       state
  FROM scheduling_lesson
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
   AND starts_at = $3::timestamptz
