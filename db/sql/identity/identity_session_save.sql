-- @no-dto
-- Одна операция и на выдачу, и на отзыв. Меняются только revoked_at: всё
-- остальное у выданной сессии не двигается — ни срок, ни отпечаток. Продлить
-- сессию правкой строки нельзя, и это осознанно.
--
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовый ip_hash не проходит identity_session_address_hashed.
INSERT INTO identity_session
            (tenant_id, id, person_id, created_at, expires_at,
             revoked_at, user_agent_hash, ip_hash)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6, $7, $8)
ON CONFLICT (tenant_id, id) DO UPDATE
        SET revoked_at = excluded.revoked_at
