SELECT tenant_id::text AS tenant_id,
       visibility,
       created_at,
       visibility_asked_at,
       visibility_decided_at,
       visibility_refusal
  FROM identity_tenant
 WHERE tenant_id = $1::uuid
