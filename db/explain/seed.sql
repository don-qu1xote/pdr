-- Данные для снятия планов: объём, при котором план осмыслен.
--
-- На пустой таблице любой план — перебор, и он ПРАВИЛЬНЫЙ: пять строк дешевле
-- прочитать целиком, чем идти в индекс. Проверять планы на пустой базе значит
-- проверять не то. Поэтому здесь двести арендаторов и десятки тысяч строк:
-- столько, чтобы выбор одного арендатора был выборкой, а не всей таблицей.
--
-- Засев идёт под ролью миграций: она владеет таблицами и суперпользователь,
-- поэтому политика на неё не действует и вставка не требует объявлять каждого
-- арендатора по очереди. Планы снимаются наоборот — под ролью приложения,
-- потому что политика добавляет условие и меняет план (docs/architecture/plans.md).
--
-- Идентификаторы детерминированные: их же подставляет scripts/check_plans.py в
-- горячие запросы. Приставка 0e0e0e0e отличает эти данные от данных проверки
-- изоляции и проверки заданий — три набора живут в одной базе и не мешают друг
-- другу.

-- Уборка прошлого прогона: засев обязан быть повторяемым.
delete from jobs_effect where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_access_log where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_login_attempt where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_one_time_token where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_session where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_credential where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_guardian_consent where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_guardianship where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_role_assignment where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_person where tenant_id in (
    select tenant_id from identity_tenant where name like 'План %'
);
delete from identity_tenant where name like 'План %';

-- Двести арендаторов. Идентификатор собирается из номера, поэтому арендатор
-- номер 7 всегда один и тот же.
insert into identity_tenant (tenant_id, name, tz)
select ('0e0e0e0e-0000-4000-8000-' || lpad(number::text, 12, '0'))::uuid,
       'План ' || number,
       'Europe/Moscow'
from generate_series(1, 200) as number;

-- Сто человек у каждого: двадцать тысяч строк, из них сто — у одного.
insert into identity_person (tenant_id, id, display_name, email, tz)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       'Человек ' || tenant || '-' || person,
       'person-' || tenant || '-' || person || '@example.test',
       'Europe/Moscow'
from generate_series(1, 200) as tenant, generate_series(1, 100) as person;

-- Роль каждому: столько же строк, ключ по человеку.
insert into identity_role_assignment (tenant_id, id, person_id, role)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0002-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       'student'
from generate_series(1, 200) as tenant, generate_series(1, 100) as person;

-- Опека на каждую вторую пару: опекун — сосед по номеру.
insert into identity_guardianship (tenant_id, id, guardian_id, student_id)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0003-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person + 1)::text, 12, '0'))::uuid
from generate_series(1, 200) as tenant, generate_series(1, 99, 2) as person;

-- Согласия опекунов: три уровня на каждую опекаемую пару, из них один
-- отозванный. Главный вопрос к таблице — «что открыто этому опекуну про этого
-- ученика», и задаётся он на КАЖДОМ обращении опекуна.
--
-- Отозванные строки в засеве не для полноты картины: индекс частичный
-- (`where revoked_at is null`), и без отозванных строк «частичный» ничего бы не
-- значило — он покрывал бы всю таблицу, и план ничего бы не доказывал.
insert into identity_guardian_consent
    (tenant_id, id, guardian_id, student_id, scope, granted_at, granted_by,
     revoked_at, revoked_by)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0008-4000-8000-' ||
        lpad((tenant * 10000 + person * 10 + level)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person + 1)::text, 12, '0'))::uuid,
       case level when 1 then 'schedule' when 2 then 'payments' else 'recordings' end,
       now() - interval '1 year',
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person + 1)::text, 12, '0'))::uuid,
       case when level = 3 then now() - interval '1 month' end,
       case when level = 3
            then ('0e0e0e0e-0001-4000-8000-' ||
                  lpad((tenant * 1000 + person + 1)::text, 12, '0'))::uuid
       end
from generate_series(1, 200) as tenant, generate_series(1, 99, 2) as person,
     generate_series(1, 3) as level;

-- Следы действий: двести пятьдесят на арендатора. Возраст распределён так, как
-- он выглядит у системы, где уборка ДЕЙСТВИТЕЛЬНО ходит: свежих следов много,
-- старше месяца — единицы. Ровно поэтому уборка обязана оставаться выборкой по
-- индексу возраста; если старым окажется полтаблицы, перебор станет правильным
-- планом, и проверять будет нечего.
insert into jobs_effect (tenant_id, job, effect_key, produced_at)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       'pdr-plans-reminders',
       'lesson-' || tenant || '-' || effect,
       now() - make_interval(days => case when effect % 50 = 0 then 31 + effect % 20
                                          else effect % 25 end)
from generate_series(1, 200) as tenant, generate_series(1, 250) as effect;

-- Журнал доступа: пять просмотров на каждую опекаемую пару. Смотрит опекун,
-- смотрят ученика — то самое «чужое», ради которого журнал и заведён.
--
-- Объём здесь нужен по той же причине, что и везде: главный вопрос к журналу —
-- «кто смотрел МОИ данные», и он обязан оставаться выборкой по одному ученику.
-- На пятидесяти тысячах строк перебор журнала уже виден, а на пяти — нет.
insert into identity_access_log (tenant_id, id, actor_id, subject_id, resource_kind, at)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0005-4000-8000-' ||
        lpad((tenant * 10000 + person * 10 + look)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person + 1)::text, 12, '0'))::uuid,
       case look % 3 when 0 then 'recording' when 1 then 'transcript' else 'chat' end,
       now() - make_interval(days => look)
from generate_series(1, 200) as tenant, generate_series(1, 99, 2) as person,
     generate_series(1, 5) as look;

-- Пароль каждому: двадцать тысяч строк. Вход ищет хеш по почте, а почта живёт
-- в другой таблице — значит, план проверяется на джойне, а не на одиночном
-- чтении.
insert into identity_credential (tenant_id, person_id, password_hash)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       '$argon2id$v=19$m=19456,t=2,p=1$c2FsdHNhbHQ$aGFzaGhhc2hoYXNoaGFzaA'
from generate_series(1, 200) as tenant, generate_series(1, 100) as person;

-- Сессия каждому. САМАЯ ГОРЯЧАЯ ТАБЛИЦА СИСТЕМЫ: её читают на КАЖДОМ запросе
-- любого человека, а не раз в занятие. Перебор здесь означает, что медленно
-- становится всё сразу, и виноватого ищут где угодно, только не тут.
insert into identity_session
    (tenant_id, id, person_id, created_at, expires_at, user_agent_hash, ip_hash)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0006-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       ('0e0e0e0e-0001-4000-8000-' || lpad((tenant * 1000 + person)::text, 12, '0'))::uuid,
       now() - interval '1 day',
       now() + make_interval(days => 1 + person % 30),
       md5('agent-' || tenant || '-' || person) || md5('agent'),
       md5('address-' || tenant) || md5('address')
from generate_series(1, 200) as tenant, generate_series(1, 100) as person;

-- Приглашения: по десять на арендатора. Ссылку ищут по отпечатку, и перебор
-- здесь стоил бы чтения всех приглашений системы на каждый переход по ссылке.
insert into identity_one_time_token
    (tenant_id, id, purpose, token_hash, role, created_at, expires_at)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0007-4000-8000-' || lpad((tenant * 100 + token)::text, 12, '0'))::uuid,
       'invitation',
       md5('token-' || tenant || '-' || token) || md5('token'),
       'student',
       now() - interval '1 hour',
       now() + interval '7 days'
from generate_series(1, 200) as tenant, generate_series(1, 10) as token;

-- Продуктовый поток: двести пятьдесят событий на арендатора. Возраст записей
-- распределён так, как он выглядит у системы, где уборка ДЕЙСТВИТЕЛЬНО ходит:
-- свежих много, старше месяца — единицы. Иначе старым окажется полтаблицы,
-- перебор станет правильным планом, и проверять будет нечего.
--
-- Типов несколько, и оценки среди них меньшинство: вопрос реестра всегда про
-- ОДИН тип события, и индекс заведён ровно под такую выборку.
insert into observability_product_event
    (tenant_id, id, type, version, actor_role, occurred_at, recorded_at, fields)
select ('0e0e0e0e-0000-4000-8000-' || lpad(tenant::text, 12, '0'))::uuid,
       ('0e0e0e0e-0004-4000-8000-' || lpad((tenant * 1000 + event)::text, 12, '0'))::uuid,
       case event % 5
           when 0 then 'reputation.rating_recorded'
           when 1 then 'scheduling.lesson_completed'
           when 2 then 'scheduling.lesson_cancelled'
           when 3 then 'content.material_opened'
           else 'notes.note_published'
       end,
       1,
       case event % 3 when 0 then 'tutor' when 1 then 'student' else 'guardian' end,
       now() - make_interval(days => case when event % 50 = 0 then 31 + event % 20
                                          else event % 25 end)
             - interval '5 minutes',
       now() - make_interval(days => case when event % 50 = 0 then 31 + event % 20
                                          else event % 25 end),
       jsonb_build_object('score', 4 + event % 2, 'low_share_in_window', 'low')
from generate_series(1, 200) as tenant, generate_series(1, 250) as event;

-- Без свежей статистики планировщик считает по умолчаниям, и план не про эти
-- данные, а про воображаемые.
analyze identity_tenant;
analyze identity_person;
analyze identity_role_assignment;
analyze identity_guardianship;
analyze identity_guardian_consent;
analyze identity_access_log;
analyze identity_credential;
analyze identity_session;
analyze identity_one_time_token;
analyze jobs_effect;
analyze observability_product_event;
