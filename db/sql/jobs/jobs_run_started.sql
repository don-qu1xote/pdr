-- @no-dto
-- Начало попытки не стирает последний завершённый прогон: пока идёт новый,
-- возраст последнего удачного — единственное, что вообще известно о задании.
--
-- Строк этот запрос не возвращает, порождать по нему нечего.
INSERT INTO jobs_run (job, attempt_at, outcome, runs)
     VALUES ($1, $2, 'running', 1)
ON CONFLICT (job) DO UPDATE
        SET attempt_at = excluded.attempt_at,
            outcome = 'running',
            runs = jobs_run.runs + 1
