-- @no-dto
-- Строк не возвращает.
--
-- ОГРАНИЧЕНИЕ НЕПЕРЕСЕЧЕНИЯ СРАБАТЫВАЕТ ЗДЕСЬ. Второй пишущий в гонке получает
-- отказ от самой базы (scheduling_lesson_no_overlap), и адаптер превращает его
-- в доменный slot_already_taken. ON CONFLICT тут не поможет и не нужен:
-- конфликтует не ключ, а отрезок времени.
INSERT INTO scheduling_lesson (tenant_id, id, series_id, tutor_id, starts_at, ends_at, tz, state)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5::timestamptz, $6::timestamptz, $7, $8)
