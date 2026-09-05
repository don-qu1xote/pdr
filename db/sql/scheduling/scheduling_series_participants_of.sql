-- Кто занимается по этой серии.
SELECT participant_id::text AS participant_id
  FROM scheduling_series_participant
 WHERE tenant_id = $1::uuid
   AND series_id = $2::uuid
 ORDER BY participant_id
