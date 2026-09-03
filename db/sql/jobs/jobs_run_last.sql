-- Незавершённый прогон записью о прогоне не считается: воркер, упавший посреди
-- работы, обязан выглядеть как замолчавшее задание, а не как отработавшее.
SELECT started_at,
       finished_at,
       outcome,
       produced,
       repeated
  FROM jobs_run
 WHERE job = $1
   AND finished_at IS NOT NULL
