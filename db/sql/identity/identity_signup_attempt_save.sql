-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовое attempts не проходит identity_signup_attempt_positive.
INSERT INTO identity_signup_attempt (address_hash, window_started_at, attempts)
     VALUES ($1, $2, $3)
ON CONFLICT (address_hash) DO UPDATE
        SET window_started_at = excluded.window_started_at,
            attempts = excluded.attempts
