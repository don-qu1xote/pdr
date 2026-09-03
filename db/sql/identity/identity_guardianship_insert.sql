-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может — в
-- образце опекун и подопечный совпадают, а identity_guardianship_not_self
-- этого не допускает.
INSERT INTO identity_guardianship
            (tenant_id, id, guardian_id, student_id, granted_at, revoked_at)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5, $6)
