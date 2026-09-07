-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая строка не проходит внешний ключ на человека.
--
-- ВЫДАЧА И ПЕРЕВЫПУСК — ОДНА СТРОКА И ОДНО ДЕЙСТВИЕ. Утёкшую ссылку чинят в
-- одно нажатие: `on conflict do update` кладёт новый отпечаток поверх старого,
-- и прежняя ссылка перестаёт работать в тот же миг — её отпечатка в базе
-- больше нет. Отдельной «отмены» не нужно, и забыть её половину нельзя.
--
-- Способ называть занятия при перевыпуске СОХРАНЯЕТСЯ: человек чинил утечку, а
-- не менял настройку.
INSERT INTO scheduling_calendar_feed (tenant_id, person_id, secret_digest, naming, issued_at)
     VALUES ($1::uuid, $2::uuid, $3, $4, $5::timestamptz)
ON CONFLICT ON CONSTRAINT scheduling_calendar_feed_pk DO UPDATE
   SET secret_digest = excluded.secret_digest,
       issued_at = excluded.issued_at
