-- История одного занятия по возрастанию момента. Под этот запрос заведён
-- scheduling_lesson_history_by_lesson (tenant_id, lesson_id, at), и другого
-- запроса к таблице нет.
SELECT actor_id::text AS actor_id,
       action,
       at,
       details
  FROM scheduling_lesson_history
 WHERE tenant_id = $1::uuid
   AND lesson_id = $2::uuid
 ORDER BY at, id
