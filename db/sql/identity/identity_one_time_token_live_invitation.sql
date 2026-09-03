-- Кого уже позвали. Условия по арендатору в запросе нет: его добавляет
-- построчная защита, и чужого приглашения этот вопрос не покажет.
SELECT id::text AS id,
       token_hash,
       role,
       created_at,
       expires_at
  FROM identity_one_time_token
 WHERE purpose = 'invitation'
   AND invited_digest = $1
   AND used_at IS NULL
   AND expires_at > $2
 LIMIT 1
