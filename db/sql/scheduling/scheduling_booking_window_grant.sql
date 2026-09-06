-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая строка не проходит scheduling_booking_window_says_something.
--
-- Послабление у пары одно: второе выданное заменяет первое, а не ложится рядом.
-- Две строки на пару означали бы вопрос «какая из них сейчас действует», на
-- который отвечать некому.
INSERT INTO scheduling_booking_window
    (tenant_id, tutor_id, student_id, book_before_minutes, horizon_minutes,
     reschedule_before_minutes, cancel_before_minutes, granted_by, granted_at)
VALUES ($1::uuid, $2::uuid, $3::uuid, $4::int, $5::int, $6::int, $7::int, $8::uuid,
        $9::timestamptz)
ON CONFLICT (tenant_id, tutor_id, student_id) DO UPDATE
   SET book_before_minutes = excluded.book_before_minutes,
       horizon_minutes = excluded.horizon_minutes,
       reschedule_before_minutes = excluded.reschedule_before_minutes,
       cancel_before_minutes = excluded.cancel_before_minutes,
       granted_by = excluded.granted_by,
       granted_at = excluded.granted_at
