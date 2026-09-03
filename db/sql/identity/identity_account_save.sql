-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая строка не проходит identity_account_confirmation_cleared.
INSERT INTO identity_account
            (id, email_digest, confirmed_at, confirmation_digest,
             confirmation_expires_at, created_at)
     VALUES ($1::uuid, $2, $3, $4, $5, $6)
ON CONFLICT (id) DO UPDATE
        SET confirmed_at = excluded.confirmed_at,
            confirmation_digest = excluded.confirmation_digest,
            confirmation_expires_at = excluded.confirmation_expires_at
