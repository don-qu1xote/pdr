-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая строка не проходит внешние ключи на человека.
--
-- Второго перерыва на те же дни у того же человека база не даст
-- (scheduling_time_off_no_overlap): «я в отпуске и одновременно болею» — это
-- один период, а не два, и решение по занятиям у него одно.
INSERT INTO scheduling_time_off
            (tenant_id, id, person_id, from_date, to_date, tz, reason, declared_by, declared_at)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4::date, $5::date, $6, $7, $8::uuid, $9::timestamptz)
