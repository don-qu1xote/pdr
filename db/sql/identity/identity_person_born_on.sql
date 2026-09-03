SELECT born_on
  FROM identity_person
 WHERE id = $1::uuid
