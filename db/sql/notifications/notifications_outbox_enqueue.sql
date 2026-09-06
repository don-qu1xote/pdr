-- @no-dto
-- Строк не возвращает.
--
-- ОДНО НАМЕРЕНИЕ — ОДНА СТРОКА. Ключ повтора у события детерминированный, и
-- второй подписчик, повторный запрос человека и перезапуск сценария приходят
-- сюда одним и тем же ключом. Без `on conflict` это были бы три письма об одном.
--
-- `do update`, а не `do nothing`, ровно ради напоминаний: занятие перенесли —
-- напоминание обязано переехать вместе с ним, а не остаться на прежнем часе.
-- Для обычного оповещения условие вырождается: срок и нагрузка те же, и запись
-- ничего не меняет.
--
-- `where state = 'pending'` — граница отзыва назад. Ушедшее письмо не
-- воскрешают: строка, помеченная `sent`, о сегодняшнем занятии больше не
-- рассказывает, а рассказывает о том, что человеку уже написали.
INSERT INTO notifications_outbox
    (tenant_id, id, event_type, payload, dedup_key, created_at, next_attempt_at)
VALUES ($1::uuid, $2::uuid, $3::text, $4::jsonb, $5::text, $6::timestamptz, $7::timestamptz)
ON CONFLICT (tenant_id, dedup_key) DO UPDATE
   SET payload = excluded.payload,
       next_attempt_at = excluded.next_attempt_at
 WHERE notifications_outbox.state = 'pending'
