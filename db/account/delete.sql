-- Удаление практики целиком.
--
-- ПЕРЕЕЗЖАТЬ К НАМ БУДУТ РОВНО НАСТОЛЬКО ОХОТНО, НАСКОЛЬКО ЛЕГКО УЕХАТЬ
-- ОБРАТНО. Поэтому удаление есть с первого дня и делается одной командой, а не
-- письмом в поддержку и не набором кнопок, между которыми человек сам
-- догадывается, что он ещё не удалил.
--
-- ПОРЯДОК ЗДЕСЬ — ЧАСТЬ ПРОВЕРКИ. Строки идут от зависимых к тем, на кого
-- ссылаются: внешние ключи не дадут удалить арендатора, пока на него что-то
-- смотрит, и забытая таблица уронит этот скрипт, а не оставит хвост в базе.
-- Ровно поэтому здесь перечисление, а не «delete from ... cascade»: каскад
-- молча уносит то, о чём никто не подумал.
--
-- Учётные записи людей (identity_account) НЕ УДАЛЯЮТСЯ. Они не принадлежат
-- практике: у ученика может быть второй репетитор, и удаление практики по
-- математике не должно отнимать у него английский (ADR-0019). Запись человека
-- уходит вместе с последней его связью — это отдельная работа, и она записана
-- долгом в docs/architecture/first-service.md.
--
-- Выгрузку делают ДО удаления: make account-export TENANT=... OUT=...
-- Зовётся целью make account-delete TENANT=<uuid>.

-- Мусор вместо идентификатора должен упасть здесь, а не удалить ничего и
-- отчитаться об успехе.
select :'tenant'::uuid \g /dev/null

begin;

delete from jobs_effect where tenant_id = :'tenant'::uuid;
delete from observability_product_event where tenant_id = :'tenant'::uuid;
delete from identity_login_attempt where tenant_id = :'tenant'::uuid;
delete from identity_one_time_token where tenant_id = :'tenant'::uuid;
delete from identity_session where tenant_id = :'tenant'::uuid;
delete from identity_credential where tenant_id = :'tenant'::uuid;
delete from identity_access_log where tenant_id = :'tenant'::uuid;
delete from identity_guardian_consent where tenant_id = :'tenant'::uuid;
delete from identity_guardianship where tenant_id = :'tenant'::uuid;
delete from identity_role_assignment where tenant_id = :'tenant'::uuid;
delete from identity_person where tenant_id = :'tenant'::uuid;
delete from identity_tenant where tenant_id = :'tenant'::uuid;

commit;

select 'практика удалена: ' || :'tenant';
