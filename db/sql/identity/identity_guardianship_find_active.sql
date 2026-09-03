SELECT granted_at
  FROM identity_guardianship
 WHERE guardian_id = $1::uuid
   AND student_id = $2::uuid
   AND revoked_at IS NULL
