-- @no-dto
-- ЗАНЯТЬ КЛЮЧ ОДНИМ ЗАПРОСОМ, а не «сначала select, потом insert».
--
-- Между двумя запросами помещается чужой insert, и оба обращения решают, что
-- ключ свободен: ровно то двойное выполнение, ради невозможности которого всё
-- написано. Здесь всё решает первичный ключ таблицы — вставка либо прошла,
-- либо не прошла, и промежутка между этими двумя ответами не существует.
--
-- on conflict do nothing вместо do update: занятый ключ трогать нельзя, а
-- узнать про него нужно — поэтому возвращающая часть собирается из union'а
-- вставленной строки и уже лежавшей. Блокировки в процессе тут не хватило бы
-- вовсе: реплик бывает больше одной, и мьютекс в одной из них про другую не
-- знает.
--
-- Строки-структуры этому запросу не порождаются: разборщик схемы ВЫПОЛНЯЕТ
-- запрос подставленными значениями, а вставка с expires_at из образца
-- нарушает http_idempotency_key_expires_after_created. Причина названа в
-- docs/architecture/queries.md, раздел «Структуры из миграций».
WITH taken AS (
    INSERT INTO http_idempotency_key
                (tenant_id, key, request_fingerprint, state, expires_at)
         VALUES ($1::uuid, $2, $3, 'in_progress', $4)
    ON CONFLICT (tenant_id, key) DO NOTHING
      RETURNING request_fingerprint, state, response_status, response_body, true AS mine
)
SELECT request_fingerprint,
       state,
       response_status,
       response_body,
       mine
  FROM taken
 UNION ALL
SELECT request_fingerprint,
       state,
       response_status,
       response_body,
       false AS mine
  FROM http_idempotency_key
 WHERE key = $2
   AND NOT EXISTS (SELECT 1 FROM taken)
