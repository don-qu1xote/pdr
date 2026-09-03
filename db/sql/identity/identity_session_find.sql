SELECT person_id::text AS person_id,
       created_at,
       expires_at,
       revoked_at,
       user_agent_hash,
       ip_hash
  FROM identity_session
 WHERE id = $1::uuid
