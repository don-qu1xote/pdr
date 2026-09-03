SELECT window_started_at,
       attempts
  FROM identity_signup_attempt
 WHERE address_hash = $1
