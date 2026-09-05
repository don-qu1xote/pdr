-- Недельные правила доступности репетитора.
--
-- Минуты от полуночи, а не тип time: «с десяти» — показание часов, а не момент,
-- и моментом оно станет только вместе с зоной (docs/architecture/migrations.md).
SELECT id::text AS id,
       weekday,
       from_minute,
       to_minute,
       tz
  FROM scheduling_availability
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
 ORDER BY weekday, from_minute
