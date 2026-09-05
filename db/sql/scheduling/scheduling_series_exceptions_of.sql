-- Отменённые и перенесённые вхождения серии.
SELECT occurrence_on,
       kind,
       moved_to,
       moved_minutes
  FROM scheduling_series_exception
 WHERE tenant_id = $1::uuid
   AND series_id = $2::uuid
 ORDER BY occurrence_on
