SELECT id::text AS id,
       email_digest,
       confirmed_at,
       confirmation_digest,
       confirmation_expires_at,
       created_at
  FROM identity_account
 WHERE id = $1::uuid
