SELECT id::text AS id,
       purpose,
       role,
       person_id::text AS person_id,
       invited_digest,
       created_at,
       expires_at,
       used_at
  FROM identity_one_time_token
 WHERE token_hash = $1
