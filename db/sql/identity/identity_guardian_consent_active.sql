SELECT id::text AS id,
       scope,
       basis,
       granted_by::text AS granted_by,
       granted_at,
       expires_at
  FROM identity_guardian_consent
 WHERE guardian_id = $1::uuid
   AND student_id = $2::uuid
   AND revoked_at IS NULL
