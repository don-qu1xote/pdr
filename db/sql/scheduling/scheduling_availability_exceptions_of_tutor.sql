-- Дни, живущие не по недельному правилу. Пустые границы означают выходной.
SELECT on_date,
       starts_at,
       ends_at
  FROM scheduling_availability_exception
 WHERE tenant_id = $1::uuid
   AND tutor_id = $2::uuid
 ORDER BY on_date
