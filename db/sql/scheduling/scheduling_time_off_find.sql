-- Перерыв по идентификатору: границы, зона, причина и принятое решение.
--
-- Идёт по первичному ключу (tenant_id, id) — своего индекса не просит.
SELECT person_id::text AS person_id,
       from_date,
       to_date,
       tz,
       reason,
       lessons_decided,
       series_decided,
       declared_by::text AS declared_by
  FROM scheduling_time_off
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
