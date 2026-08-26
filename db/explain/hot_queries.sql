-- Горячие запросы: те, чей ПЛАН проверяется в CI.
--
-- Список лежит файлом, а не в голове: «этот запрос обязан ходить по индексу» —
-- утверждение, которое либо записано и проверяется, либо не существует. Индекс,
-- переставший применяться, ничем себя не проявляет: запрос отвечает то же самое,
-- только на два порядка дольше и только на живых данных.
--
-- Планы снимаются под ролью приложения и с объявленным арендатором: политика RLS
-- добавляет к запросу условие по tenant_id, и план без неё — план другого
-- запроса. Правила списка и что делать, когда джоба покраснела, —
-- docs/architecture/plans.md.
--
-- Формат блока: строки «-- ключ: значение», затем сам запрос до точки с запятой.
--
--   запрос      имя случая, оно же имя в выводе джобы;
--   откуда      чем оправдан: файл адаптера, порт или индекс, ради которого он
--               заведён. Запрос без «откуда» — догадка, а не горячий запрос;
--   индекс      какой индекс ОБЯЗАН оказаться в плане. Несколько через запятую
--               означают «любой из»;
--   перебор     таблицы, где Seq Scan допустим и почему. Без этого ключа любой
--               Seq Scan в плане роняет джобу.
--
-- Подстановки: {tenant} {person} {guardian} {email} {session} {token} {job}
--             {keep_days} —
-- их заполняет scripts/check_plans.py значениями из db/explain/seed.sql.

-- запрос: identity_person_by_tenant
-- откуда: список людей арендатора; здесь политика — единственное условие, и это
--         ровно тот случай, ради которого заведён identity_person_by_tenant
-- индекс: identity_person_by_tenant
select id, display_name
  from identity_person;

-- запрос: identity_person_by_id
-- откуда: человек по идентификатору — самый частый доступ любого сценария
-- индекс: identity_person_pk
select display_name, email, tz
  from identity_person
 where id = '{person}';

-- запрос: identity_person_by_email
-- откуда: вход и поиск по почте; identity_person_email_unique заведён для этого
-- индекс: identity_person_email_unique
select id, display_name
  from identity_person
 where email = '{email}';

-- запрос: identity_guardianship_active_pair
-- откуда: identity::ports::GuardianshipRepository::FindActive
-- индекс: identity_guardianship_by_student
select id, granted_at
  from identity_guardianship
 where guardian_id = '{guardian}'
   and student_id = '{person}'
   and revoked_at is null;

-- запрос: identity_guardianship_by_student
-- откуда: identity::Contract::MayActFor — кто вправе действовать за ученика
-- индекс: identity_guardianship_by_student
select id, guardian_id
  from identity_guardianship
 where student_id = '{person}'
   and revoked_at is null;

-- запрос: identity_role_assignment_by_person
-- откуда: роли человека в тенанте; проверяются на каждом обращении
-- индекс: identity_role_assignment_by_person
select id, role
  from identity_role_assignment
 where person_id = '{person}'
   and revoked_at is null;

-- запрос: identity_session_by_id
-- откуда: identity::ports::SessionStore::Find — САМЫЙ ЧАСТЫЙ запрос системы:
--         он идёт на каждом обращении любого человека, а не раз в занятие
-- индекс: identity_session_pk
select person_id, expires_at, revoked_at
  from identity_session
 where id = '{session}';

-- запрос: identity_credential_by_email
-- откуда: identity::ports::CredentialStore::FindByEmail — вход; пароль лежит
--         отдельно от человека, поэтому план проверяется на джойне
-- индекс: identity_person_email_unique
select c.person_id, c.password_hash
  from identity_credential c
  join identity_person p on p.tenant_id = c.tenant_id and p.id = c.person_id
 where p.email = '{email}';

-- запрос: identity_one_time_token_by_hash
-- откуда: identity::ports::OneTimeTokens::Find — переход по ссылке из письма
-- индекс: identity_one_time_token_secret_unique
select id, purpose, role, person_id, expires_at, used_at
  from identity_one_time_token
 where token_hash = '{token}';

-- запрос: identity_access_log_by_subject
-- откуда: «кто смотрел мои данные» — единственный вопрос к журналу доступа,
--         ради него и заведён identity_access_log_by_subject
-- индекс: identity_access_log_by_subject
select actor_id, resource_kind, at
  from identity_access_log
 where subject_id = '{person}'
 order by at desc
 limit 50;

-- запрос: jobs_effect_cleanup
-- откуда: уборка старых следов, ради которой заведён jobs_effect_by_age
--         (db/migrations/V003__jobs.sql). Уборка идёт под арендатором, как и
--         всё остальное: политика для фоновой работы исключений не делает
-- индекс: jobs_effect_by_age
delete from jobs_effect
 where produced_at < now() - make_interval(days => {keep_days});

-- запрос: observability_product_event_cleanup
-- откуда: уборка продуктового потока по сроку жизни (db/observability/prune.sql),
--         ради которой заведён observability_product_event_by_age. Поток растёт
--         быстрее любой другой таблицы, и перебор здесь стоит дороже всего
-- индекс: observability_product_event_by_age
delete from observability_product_event
 where recorded_at < now() - make_interval(days => {keep_days});

-- запрос: observability_product_event_by_type
-- откуда: чтение под вопрос реестра — «все оценки за последнее время»
--         (docs/product/open-questions.md, вопрос rating_inflation). Вопрос
--         всегда про ОДИН тип события, ради этого и заведён
--         observability_product_event_by_type
-- индекс: observability_product_event_by_type
select occurred_at, actor_role, fields
  from observability_product_event
 where type = 'reputation.rating_recorded'
   and occurred_at >= now() - make_interval(days => {keep_days});
