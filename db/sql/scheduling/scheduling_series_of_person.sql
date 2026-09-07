-- Серии, которые касаются человека, — и как репетитора, и как участника.
--
-- ОДНИМ ЗАПРОСОМ, ДВУМЯ ИНДЕКСАМИ. Половинки идут каждая по своему индексу —
-- scheduling_series_by_tutor и scheduling_series_by_participant, — а UNION
-- убирает повтор у того, кто в своей же серии стоит обеими сторонами. Развилки
-- «а он репетитор или ученик?» здесь нет намеренно: отпуск и каникулы — один
-- механизм, и спрашивающему незачем знать сторону.
--
-- Возвращаются ИДЕНТИФИКАТОРЫ, а не серии: серия — это правило плюс участники
-- плюс исключения, и собирает её scheduling_series_find. Второй сборки того же
-- не заводим, иначе они разойдутся молча.
SELECT id::text AS id
  FROM scheduling_series
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
 UNION
SELECT series_id::text AS id
  FROM scheduling_series_participant
 WHERE tenant_id = $1::uuid
   AND participant_id = $2::uuid
