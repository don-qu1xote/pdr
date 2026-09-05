-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовые минуты не проходят scheduling_availability_forward.
--
-- ПАЧКОЙ: у репетитора недельных правил бывает десяток, и платить за каждое
-- круговым ходом незачем.
INSERT INTO scheduling_availability (tenant_id, id, tutor_id, weekday, from_minute, to_minute, tz)
     SELECT *
       FROM unnest($1::uuid[], $2::uuid[], $3::uuid[], $4::smallint[],
                   $5::smallint[], $6::smallint[], $7::text[])
