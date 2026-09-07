-- @no-dto
-- Строк не возвращает; разбору мешает scheduling_time_off_decided_whole:
-- у образцовой строки решение заполнено наполовину.
--
-- РЕШЕНИЕ ПРИНИМАЮТ ОДИН РАЗ. Условие `lessons_decided is null` — не
-- оптимистичная блокировка, а само правило: занятия уже отменены или
-- перенесены, и «примените ещё раз, но по-другому» означало бы отменять
-- отменённое. Ноль изменённых строк адаптер превращает в названный отказ.
UPDATE scheduling_time_off
   SET lessons_decided = $3,
       series_decided = $4,
       decided_at = $5::timestamptz
 WHERE tenant_id = $1::uuid
   AND id = $2::uuid
   AND lessons_decided IS NULL
