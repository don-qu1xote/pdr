-- @no-dto
-- Строк не возвращает. Та же причина, что у соседнего запроса.
DELETE FROM scheduling_availability_exception
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
