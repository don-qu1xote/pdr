-- Серия по идентификатору: само правило и всё, что нужно развёртке.
SELECT tutor_id::text AS tutor_id,
       rrule,
       starts_on,
       at_minute,
       tz,
       duration_minutes
  FROM scheduling_series
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
