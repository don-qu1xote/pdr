SELECT role
  FROM identity_role_assignment
 WHERE person_id = $1::uuid
   AND revoked_at IS NULL
