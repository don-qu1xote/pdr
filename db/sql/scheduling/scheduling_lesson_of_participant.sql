-- Занятия участника за диапазон.
--
-- Участник живёт отдельной таблицей (вектор в домене), поэтому и путь другой:
-- scheduling_lesson_by_participant отбирает занятия человека, первичный ключ
-- занятия достаёт сами строки. Колонки starts_at в таблице участников нет
-- намеренно: это была бы вторая копия начала занятия, и разошлась бы она молча.
SELECT l.id::text AS id,
       l.tutor_id::text AS tutor_id,
       l.series_id::text AS series_id,
       l.starts_at,
       l.ends_at,
       l.tz,
       l.state
  FROM scheduling_lesson_participant AS p
  JOIN scheduling_lesson AS l
    ON l.tenant_id = p.tenant_id
   AND l.id = p.lesson_id
 WHERE p.tenant_id = $1::uuid
   AND p.participant_id = $2::uuid
   AND l.starts_at >= $3::timestamptz
   AND l.starts_at < $4::timestamptz
 ORDER BY l.starts_at
