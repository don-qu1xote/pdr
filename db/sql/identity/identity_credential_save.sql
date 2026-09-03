-- @no-dto
-- Смена пароля — это правка той же строки, а не вторая строка рядом: две
-- действующие пары «человек и пароль» не выражаются в схеме, и правильно.
--
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовый password_hash не проходит identity_credential_argon2id.
INSERT INTO identity_credential (tenant_id, person_id, password_hash, updated_at)
     VALUES ($1::uuid, $2::uuid, $3, now())
ON CONFLICT (tenant_id, person_id) DO UPDATE
        SET password_hash = excluded.password_hash,
            updated_at = excluded.updated_at
