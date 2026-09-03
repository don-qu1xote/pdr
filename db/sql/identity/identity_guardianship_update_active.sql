UPDATE identity_guardianship
   SET granted_at = $3,
       revoked_at = $4
 WHERE guardian_id = $1::uuid
   AND student_id = $2::uuid
   AND revoked_at IS NULL
