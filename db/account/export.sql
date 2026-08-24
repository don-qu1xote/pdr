-- Полная выгрузка аккаунта одним действием.
--
-- «Данные ваши» — обещание, которое подтверждается не абзацем в оферте, а
-- работающей командой. Выгрузка одна и отдаёт всё сразу: обещание не
-- выполняется набором кнопок «скачать вот эту часть», между которыми человек
-- сам догадывается, что он ещё не скачал.
--
-- ГРАНИЦУ ДЕРЖИТ ПОЛИТИКА, А НЕ ЗАПРОС. Ниже нет ни одного условия по
-- арендатору: выгрузка идёт под ролью приложения с объявленным арендатором, и
-- строки отбирает RLS. Расширить выгрузку случайной правкой запроса поэтому
-- нельзя — она отдаёт ровно то, что видит сам аккаунт, и ни строкой больше.
--
-- Колонки не перечисляются: to_jsonb(строка) берёт их все. Новая колонка
-- попадает в выгрузку сама, а не через полгода после того, как её завели.
--
-- Что входит, чего нет и почему — docs/architecture/openness.md; расхождение
-- этого файла со схемой роняет сборку (scripts/check_openness.py).
--
-- Зовётся целью make account-export TENANT=<uuid> OUT=<файл>.

-- Мусор вместо идентификатора должен упасть здесь, а не превратиться в пустую
-- выгрузку, которую примут за «данных нет».
select :'tenant'::uuid \g /dev/null

set role pdr_app;
select set_config('pdr.tenant_id', :'tenant', false) \g /dev/null

select jsonb_pretty(jsonb_build_object(
    'format', 'pdr-account-export/1',
    'exported_at', to_char(now() at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
    'tenant', :'tenant',
    'parts', jsonb_build_object(
        'identity_tenant',
        coalesce((select jsonb_agg(to_jsonb(t) order by t.tenant_id)
                    from identity_tenant t), '[]'::jsonb),
        'identity_person',
        coalesce((select jsonb_agg(to_jsonb(t) order by t.id)
                    from identity_person t), '[]'::jsonb),
        'identity_role_assignment',
        coalesce((select jsonb_agg(to_jsonb(t) order by t.id)
                    from identity_role_assignment t), '[]'::jsonb),
        'identity_guardianship',
        coalesce((select jsonb_agg(to_jsonb(t) order by t.id)
                    from identity_guardianship t), '[]'::jsonb),
        'observability_product_event',
        coalesce((select jsonb_agg(to_jsonb(t) order by t.recorded_at, t.id)
                    from observability_product_event t), '[]'::jsonb)
    )
));

reset role;
