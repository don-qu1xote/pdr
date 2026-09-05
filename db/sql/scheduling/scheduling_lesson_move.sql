-- @no-dto
-- Строк не возвращает. ПЕРЕНОС МЕНЯЕТ ВРЕМЯ, А НЕ ЛИЧНОСТЬ: строка та же, id
-- тот же, и ограничение непересечения проверяется здесь же — на новом месте.
UPDATE scheduling_lesson
   SET starts_at = $3::timestamptz,
       ends_at = $4::timestamptz
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
