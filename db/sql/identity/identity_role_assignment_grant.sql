-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая роль не проходит identity_role_assignment_role_known.
INSERT INTO identity_role_assignment (tenant_id, id, person_id, role)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4)
