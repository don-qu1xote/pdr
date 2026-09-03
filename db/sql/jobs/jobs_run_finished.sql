UPDATE jobs_run
   SET started_at = $2,
       finished_at = $3,
       duration_ms = $4,
       outcome = $5,
       produced = $6,
       repeated = $7
 WHERE job = $1
