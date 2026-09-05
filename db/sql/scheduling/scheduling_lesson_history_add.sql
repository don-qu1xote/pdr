-- @no-dto
-- Строк не возвращает; разобрать запрос разборщик к тому же не может —
-- образцовая строка не проходит scheduling_lesson_history_action_known.
--
-- Строка истории. Только вставка: правок и удалений у истории не бывает, и
-- прав на них у прикладной роли тоже нет.
INSERT INTO scheduling_lesson_history
    (tenant_id, id, lesson_id, actor_id, action, at, details)
VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5::text, $6::timestamptz, $7::text)
