-- Занятия репетитора за диапазон. Под этот запрос заведён
-- scheduling_lesson_by_tutor (tenant_id, tutor_id, starts_at), и EXPLAIN на
-- месячной выборке показывает Index Scan по нему, а не Seq Scan.
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
   AND starts_at >= $3::timestamptz
   AND starts_at < $4::timestamptz
 ORDER BY starts_at
