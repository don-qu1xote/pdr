-- @no-dto
-- Строк не возвращает; разбору мешает scheduling_availability_exception_whole:
-- у образцовой строки границы заполнены наполовину.
INSERT INTO scheduling_availability_exception
            (tenant_id, tutor_id, on_date, starts_at, ends_at)
     SELECT *
       FROM unnest($1::uuid[], $2::uuid[], $3::date[],
                   $4::timestamptz[], $5::timestamptz[])
