-- @no-dto
-- Строк не возвращает.
--
-- ЧТО СТАЛО С ОДНИМ УЧАСТИЕМ: цена, оплата, исход, выход. Одним запросом, а не
-- четырьмя: они меняются вместе — занятие провели, и в тот же миг у участия
-- появились и цена, и отметка о присутствии.
--
-- Само занятие эта строка не трогает: выход участника — не отмена занятия, и
-- состояние занятия живёт в другой таблице.
UPDATE scheduling_lesson_participant
   SET price_minor = $4::bigint,
       currency = $5::char(3),
       payment = $6::text,
       attendance = $7::text,
       state = $8::text
 WHERE tenant_id = $1::uuid
   AND lesson_id = $2::uuid
   AND participant_id = $3::uuid
