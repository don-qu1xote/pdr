-- @no-dto
-- Строк не возвращает. Переход разрешил домен (scheduling::Transition), здесь
-- только запись: машина состояний живёт в одном месте, а не в двух.
UPDATE scheduling_lesson
   SET state = $3
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
