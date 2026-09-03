SELECT actor_id::text AS actor_id,
       resource_kind,
       outcome,
       at
  FROM identity_access_log
 WHERE subject_id = $1::uuid
   AND at >= $2
 ORDER BY at DESC
