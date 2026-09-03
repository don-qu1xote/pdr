SELECT password_hash
  FROM identity_credential
 WHERE person_id = $1::uuid
