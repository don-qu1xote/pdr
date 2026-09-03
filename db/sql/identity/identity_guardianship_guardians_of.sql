SELECT guardian_id::text AS guardian_id
  FROM identity_guardianship
 WHERE student_id = $1::uuid
   AND revoked_at IS NULL
 ORDER BY granted_at
