-- Вход в продукт: практика, приглашения списком и один человек на всю площадку.
--
-- Задача не про формы регистрации, а про то, чтобы репетитор с двадцатью
-- учениками перенёс их к нам и работал, ни разу не увидев подбора. Отсюда всё
-- ниже: практика заводится скрытой, приглашение помнит адрес, а человек —
-- один, сколько бы репетиторов у него ни было.

-- УЧЁТНАЯ ЗАПИСЬ: ОДИН ЧЕЛОВЕК НА ВСЮ ПЛОЩАДКУ.
--
-- Единственная таблица дерева БЕЗ tenant_id и без построчной защиты, и это
-- решение, а не упущение (docs/adr/0019-one-account-many-practices.md).
-- Математику ученик учит у одного репетитора, английский у другого — обычное
-- дело; без общего реестра «тот же самый человек» не выражается вовсе, и у
-- Маши оказалось бы два идентификатора, которые потом склеивают руками.
--
-- ПЕРСОНАЛЬНЫХ ДАННЫХ ЗДЕСЬ НЕТ. Через границу арендатора едет отпечаток
-- почты, а не почта: по отпечатку человек находится, из отпечатка не
-- восстанавливается ни адрес, ни имя, ни чей он ученик. Имя, зона и сама почта
-- живут в identity_person — внутри практики, под RLS.
--
-- И ЗДЕСЬ НЕТ НИЧЕГО УЧЕБНОГО. Ни прогресса, ни целей, ни числа «готовности
-- вообще»: такое число потребовало бы решить, чей предмет в нём главный, а
-- верного ответа нет. Состав колонок заперт списком в scripts/check_rls.py —
-- новая колонка сюда не заводится молча.
create table identity_account (
    id                       uuid        not null,
    email_digest             char(64)    not null,
    confirmed_at             timestamptz,
    confirmation_digest      char(64),
    confirmation_expires_at  timestamptz,
    created_at               timestamptz not null default now(),
    constraint identity_account_pk primary key (id),
    constraint identity_account_mail_unique unique (email_digest),
    constraint identity_account_digest_lowercase
        check (email_digest = lower(email_digest)),
    -- Подтверждённой записи подтверждать нечего: ссылка гасится вместе с
    -- подтверждением, иначе она осталась бы рабочей навсегда.
    constraint identity_account_confirmation_cleared
        check (confirmed_at is null or confirmation_digest is null),
    constraint identity_account_confirmation_whole
        check ((confirmation_digest is null) = (confirmation_expires_at is null))
);
comment on table identity_account is 'Один человек на всю площадку: отпечаток почты и идентификатор. Единственная таблица без tenant_id — ADR-0019.';

-- Счётчик самостоятельных заведений с одного адреса.
--
-- Тоже без арендатора, и по той же причине: заводящийся человек ещё ни в каком
-- арендаторе не состоит, считать его там негде. В базе, а не в памяти
-- процесса: в памяти порог удваивается со второй репликой.
create table identity_signup_attempt (
    address_hash      char(64)    not null,
    window_started_at timestamptz not null,
    attempts          integer     not null,
    constraint identity_signup_attempt_pk primary key (address_hash),
    constraint identity_signup_attempt_positive check (attempts > 0),
    constraint identity_signup_attempt_hash_lowercase
        check (address_hash = lower(address_hash))
);
comment on table identity_signup_attempt is 'Сколько раз с этого адреса заводились сами. Ни почты, ни адреса в открытом виде — только отпечаток.';

-- Уборка счётчика идёт по возрасту окна: отработавшие окна не нужны никому.
create index identity_signup_attempt_by_age on identity_signup_attempt (window_started_at);

grant select, insert, update on identity_account to pdr_app;
grant select, insert, update, delete on identity_signup_attempt to pdr_app;

-- ВИДИМОСТЬ ПРАКТИКИ СНАРУЖИ. Выключена у всех по умолчанию — не осторожность,
-- а форма продукта: развилки «зачем вы пришли» при регистрации нет, а значит
-- нет и вопроса «показывать ли вас в поиске». Ответ на него один для всех, и
-- он «нет», пока хозяин сам не попросит об обратном.
--
-- Разбор руками стоит на ПУБЛИКАЦИИ, а не на регистрации: очередь на входе
-- означает, что репетитор с двадцатью учениками ждёт, пока его посмотрят.
alter table identity_tenant add column visibility text not null default 'hidden';
alter table identity_tenant add constraint identity_tenant_visibility_known
    check (visibility in ('hidden', 'pending', 'published', 'refused'));
alter table identity_tenant add column visibility_asked_at timestamptz;
alter table identity_tenant add column visibility_decided_at timestamptz;
alter table identity_tenant add column visibility_refusal text;
alter table identity_tenant add constraint identity_tenant_refusal_known
    check (visibility_refusal is null
           or visibility_refusal in ('nothing_to_show', 'looks_borrowed', 'calls_away'));
-- Причина отказа бывает только у отказа. Иначе «почему отказали» отвечает
-- строка, оставшаяся от прошлого разбора.
alter table identity_tenant add constraint identity_tenant_refusal_only_when_refused
    check (visibility_refusal is null or visibility = 'refused');
comment on column identity_tenant.visibility is 'Видна ли практика в подборе. hidden у всех по умолчанию; published — только после разбора.';

-- Очередь на разбор: тех, кто ждёт, единицы против всех практик, поэтому
-- индекс частичный.
create index identity_tenant_awaiting_review on identity_tenant (visibility_asked_at)
    where visibility = 'pending';

-- КОГО ПОЗВАЛИ. Отпечаток адреса, а не адрес: строка приглашения живёт неделю,
-- и всё это время отпечатка хватает, чтобы не послать второго письма тому же
-- человеку, и не хватает, чтобы прочитать список учеников из дампа.
--
-- Пусто у ссылок «позвать кого угодно» и у сбросов пароля.
alter table identity_one_time_token add column invited_digest char(64);
alter table identity_one_time_token add constraint identity_one_time_token_invited_lowercase
    check (invited_digest is null or invited_digest = lower(invited_digest));
comment on column identity_one_time_token.invited_digest is 'Отпечаток адреса, на который выдано приглашение. Ради него повтор не шлёт второго письма.';

-- Главный вопрос к приглашениям при вставке списка: «этому уже писали?».
-- Спрашивается по разу на каждую строку списка из двадцати, поэтому индекс.
create index identity_one_time_token_invited
    on identity_one_time_token (tenant_id, invited_digest)
    where invited_digest is not null and used_at is null;
