-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовое name пусто, а identity_tenant_name_not_blank этого не допускает.
INSERT INTO identity_tenant (tenant_id, name, tz, visibility, created_at)
     VALUES ($1::uuid, $2, $3, $4, $5)
ON CONFLICT (tenant_id) DO NOTHING
