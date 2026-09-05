-- @no-dto
-- Строк не возвращает; разбору мешает
-- scheduling_series_exception_place_matches_kind: у образцовой строки вид и
-- новое время не сходятся.
--
-- ON CONFLICT DO NOTHING, а не UPDATE: второе исключение на ту же дату — это
-- «отменено и перенесено одновременно», и домен уже сказал, что так нельзя.
-- Ноль изменённых строк адаптер превращает в тот же отказ, что и домен.
INSERT INTO scheduling_series_exception
            (tenant_id, series_id, occurrence_on, kind, moved_to, moved_minutes)
     VALUES ($1::uuid, $2::uuid, $3::date, $4, $5::timestamptz, $6::integer)
ON CONFLICT ON CONSTRAINT scheduling_series_exception_pk DO NOTHING
