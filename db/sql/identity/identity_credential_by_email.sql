SELECT c.person_id::text AS person_id,
       c.password_hash
  FROM identity_credential c
  JOIN identity_person p
    ON p.tenant_id = c.tenant_id
   AND p.id = c.person_id
 WHERE p.email = $1
