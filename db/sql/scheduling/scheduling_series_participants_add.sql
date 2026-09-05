-- @no-dto
-- Строк не возвращает. Пачкой по той же причине, что у занятия.
INSERT INTO scheduling_series_participant (tenant_id, series_id, participant_id)
     SELECT *
       FROM unnest($1::uuid[], $2::uuid[], $3::uuid[])
