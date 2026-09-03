-- @no-dto
-- Отзыв — правка строки, а не новая рядом: частичный уникальный индекс не
-- позволил бы двум действующим согласиям на пару и уровень, и правильно.
--
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовый basis не проходит identity_guardian_consent_basis_known.
INSERT INTO identity_guardian_consent
            (tenant_id, id, guardian_id, student_id, scope, basis,
             granted_at, granted_by, expires_at, revoked_at, revoked_by)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5, $6,
             $7, $8::uuid, $9, $10, $11::uuid)
ON CONFLICT (tenant_id, id) DO UPDATE
        SET revoked_at = excluded.revoked_at,
            revoked_by = excluded.revoked_by
