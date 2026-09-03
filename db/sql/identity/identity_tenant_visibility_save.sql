UPDATE identity_tenant
   SET visibility = $2,
       visibility_asked_at = $3,
       visibility_decided_at = $4,
       visibility_refusal = $5
 WHERE tenant_id = $1::uuid
