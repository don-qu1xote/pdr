-- Записать ответ и закрыть ключ. Условие по state — не украшение: закрыть
-- чужой завершённый ключ значило бы переписать ответ, который уже отдали.
UPDATE http_idempotency_key
   SET state = 'completed',
       response_status = $3,
       response_body = $4
 WHERE key = $2
   AND tenant_id = $1::uuid
   AND state = 'in_progress'
