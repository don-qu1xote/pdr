-- @no-dto
-- Строк не возвращает.
--
-- Серия хранится ПРАВИЛОМ: rrule текстом, время — минутами от полуночи вместе
-- с зоной. Развёрнутого списка занятий здесь нет и не будет (PDR-SCHED-02):
-- сорок строк, созданных при заведении, ломаются на первом же переносе.
INSERT INTO scheduling_series
            (tenant_id, id, tutor_id, rrule, starts_on, at_minute, tz, duration_minutes)
     VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5::date, $6::smallint, $7, $8::integer)
