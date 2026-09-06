-- Послабление окон для одной пары. Строки может не быть, и это обычный случай:
-- у большинства пар послаблений нет и никогда не будет.
SELECT book_before_minutes,
       horizon_minutes,
       reschedule_before_minutes,
       cancel_before_minutes
  FROM scheduling_booking_window
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
   AND student_id = $3::uuid
