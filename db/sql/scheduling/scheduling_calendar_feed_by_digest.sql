-- Подписка по отпечатку предъявленного секрета — внутри кабинета из адреса.
--
-- Идёт по scheduling_calendar_feed_by_digest (tenant_id, secret_digest).
SELECT person_id::text AS person_id,
       naming
  FROM scheduling_calendar_feed
 WHERE tenant_id = $1::uuid
   AND secret_digest = $2
