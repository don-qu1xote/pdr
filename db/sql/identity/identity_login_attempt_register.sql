-- @no-dto
-- $4 — «сейчас», $5 — «начало окна, которое уже истекло». Сравнение с $5 и есть
-- то самое правило домена, только записанное там, где оно неделимо.
--
-- Разобрать запрос разборщик не может: образцовый subject_hash не проходит
-- identity_login_attempt_hashed, — поэтому строки читаются по именам колонок.
INSERT INTO identity_login_attempt
            (tenant_id, subject_kind, subject_hash, window_started_at, attempts)
     VALUES ($1::uuid, $2, $3, $4, 1)
ON CONFLICT (tenant_id, subject_kind, subject_hash) DO UPDATE
        SET window_started_at = CASE
                WHEN identity_login_attempt.window_started_at <= $5
                THEN excluded.window_started_at
                ELSE identity_login_attempt.window_started_at
            END,
            attempts = CASE
                WHEN identity_login_attempt.window_started_at <= $5
                THEN 1
                ELSE identity_login_attempt.attempts + 1
            END
  RETURNING window_started_at, attempts
