-- Очередь на разбор публикации: кто попросил показывать себя в подборе.
--
-- РАЗБОР СТОИТ НА ПУБЛИКАЦИИ, А НЕ НА РЕГИСТРАЦИИ. Всё это время практика
-- работает: занятия идут, ученики приглашены, деньги считаются. Разбирается
-- один вопрос — показывать ли эту практику незнакомым людям.
--
-- Идёт под ролью миграций, а не под ролью приложения: очередь общая, поверх
-- всех практик, и построчная защита такого вопроса не пропускает — она и
-- заведена, чтобы не пропускать. Это работа оператора, а не сценария
-- (docs/architecture/onboarding.md).
--
-- Зовётся целью make practice-queue.

select jsonb_pretty(coalesce(jsonb_agg(jsonb_build_object(
           'tenant', t.tenant_id,
           'name', t.name,
           'asked_at', to_char(t.visibility_asked_at at time zone 'UTC',
                               'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
           'people', (select count(*) from identity_person p
                       where p.tenant_id = t.tenant_id),
           'was_refused_for', t.visibility_refusal
       ) order by t.visibility_asked_at), '[]'::jsonb))
  from identity_tenant t
 where t.visibility = 'pending';
