-- @no-dto
-- on conflict do nothing и проверка числа строк вместо отлова исключения:
-- занятая почта — ожидаемый отказ предметной области, а не авария. Ловить его
-- исключением значило бы разбирать код SQLSTATE и надеяться, что это был именно
-- тот уникальный ключ.
--
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовое display_name пусто, а identity_person_display_name_not_blank
-- этого не допускает.
INSERT INTO identity_person (tenant_id, id, display_name, email, tz, born_on)
     VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6)
ON CONFLICT (tenant_id, email) DO NOTHING
