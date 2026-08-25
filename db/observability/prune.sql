-- Уборка продуктового потока по сроку жизни.
--
-- Идёт по recorded_at, а не по occurred_at: событие с уехавшим временем
-- издателя не должно ни выживать вечно, ни исчезать раньше остальных.
--
-- Числа здесь нет и не будет: срок живёт в PDR_PRODUCT_EVENTS.retention_days, и
-- второго источника правды не заводится. Цель требует его явно:
-- make product-events-prune DAYS=<сколько хранить>.
--
-- Задания, которое звало бы уборку само, ещё нет — это долг первого сервиса
-- (docs/architecture/first-service.md). План этого запроса проверяется в CI:
-- db/explain/hot_queries.sql, случай observability_product_event_cleanup.

delete from observability_product_event
 where recorded_at < now() - make_interval(days => :days);
