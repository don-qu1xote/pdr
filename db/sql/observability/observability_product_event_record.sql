-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовый jsonb не проходит проверку observability_product_event_fields.
INSERT INTO observability_product_event
            (tenant_id, id, type, version, actor_role, occurred_at, fields)
     VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::jsonb)
