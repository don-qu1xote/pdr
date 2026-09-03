-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может — в
-- образце expires_at не позже created_at, а
-- identity_one_time_token_expires_after_created этого не допускает.
INSERT INTO identity_one_time_token
            (tenant_id, id, purpose, token_hash, role, person_id,
             invited_digest, created_at, expires_at)
     VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6::uuid, $7, $8, $9)
