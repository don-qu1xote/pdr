# Постулаты и чем они обеспечены

**Постулат без теста — лозунг.** Список правил, который никто не проверяет,
через полгода описывает не продукт, а намерения полугодовой давности; при этом
выглядит он ровно так же убедительно.

Поэтому здесь сверка, а не декларация: у каждого постулата сказано, где он
обеспечен в дереве и чем проверяется. Там, где теста нет, стоит слово
**намерение** и причина — не как оправдание, а чтобы отличать сделанное от
обещанного одним взглядом.

Расхождение роняет сборку: `scripts/check_openness.py` требует, чтобы в столбце
«каким тестом» стояла либо ссылка на существующий файл, цель сборки или прогон
ctest, либо пометка «намерение».

## Сверка

| Постулат | Где обеспечен | Каким тестом |
| --- | --- | --- |
| Изоляция арендаторов структурная: RLS, а не аккуратный запрос | `db/migrations/V002__init.sql` | `scripts/check_rls.py`, `scripts/check_isolation.py` |
| Забытый арендатор даёт пустой ответ, а не чужие строки | `libs/pdr-core/src/infrastructure/postgres_tenant_aware_repository.cpp` | `scripts/check_isolation.py`, `libs/pdr-testing/include/pdr/testing/repository_contract.hpp` |
| Сессию хранилища не получить мимо области арендатора | `libs/pdr-core/src/application/ports/tenant_aware_repository.hpp` | `tenant_session_outside_scope` |
| Время — `timestamptz` в UTC плюс отдельная зона IANA | `libs/pdr-core/src/core/types/time.cpp` | `scripts/check_migrations.py`, `libs/pdr-core/tests/time_test.cpp` |
| Деньги — целые минорные единицы и код валюты | `libs/pdr-core/src/core/money.cpp` | `scripts/check_migrations.py`, `libs/pdr-billing/tests/quote_test.cpp` |
| Идентификаторы разных сущностей не путаются местами | `libs/pdr-core/src/core/types/ids.hpp` | `ids_person_as_tenant`, `ids_implicit_to_string` |
| `core` не знает ни userver, ни базы, ни конфигов | `CMakeLists.txt` | `scripts/check_layers.py`, `config_in_core` |
| Контекст виден соседу ровно одним заголовком | `libs/pdr-identity/contract/identity/contract.hpp` | `scripts/check_layers.py`, `identity_internals` |
| Часы и генератор идентификаторов — порты, а не системный вызов | `libs/pdr-core/src/application/ports/clock.hpp` | `libs/pdr-testing/include/pdr/testing/clock_contract.hpp`, `contract` |
| Unit-прогон не ходит в базу — потому что не линкуется с ней | `CMakeLists.txt` | `unit` |
| Карта контекстов не расходится с миграциями | `docs/architecture/context-map.md` | `scripts/check_table_owners.py` |
| Применённая миграция никогда не редактируется | `scripts/migrate.py` | `migrate-verify` |
| Индекс либо применяется, либо нет — и это видно | `db/explain/hot_queries.sql` | `scripts/check_plans.py` |
| Одиночное периодическое задание делает ровно один воркер | `libs/pdr-jobs/src/jobs/infrastructure/periodic_job_component.cpp` | `scripts/check_jobs.py` |
| Чужой API не бывает несущим | `docs/architecture/integrations.md` | `scripts/check_integrations.py`, `contract.load_bearing_integration_must_fail` |
| У каждого ИИ-узла есть своя реализация без сети | `docs/architecture/ai-sovereignty.md` | `scripts/check_sovereignty.py`, `contract.network_bound_product_must_fail` |
| Штатное вместо самодельного | `docs/adr/0013-standard-over-handmade.md` | `scripts/check_handmade.py` |
| Число, влияющее на людей, живёт в динамическом конфиге | `configs/dynamic/registry.yaml` | `scripts/check_dynamic_configs.py` |
| Событие без вопроса не заводится, вопрос без события — тоже | `configs/product-events.yaml` | `scripts/check_product_events.py` |
| В продуктовом событии нет идентификатора человека | `libs/pdr-observability/contract/observability/contract.hpp` | `person_in_product_event`, `scripts/check_product_events.py` |
| Опубликованная схема события не меняется | `configs/product-events.published.yaml` | `scripts/check_product_events.py` |
| Секретов нет ни в окружении, ни в истории | `deploy/README.md` | `scripts/check_secrets.py` |
| Инженерного термина в интерфейсе нет | `docs/product/glossary.md` | `scripts/check_glossary.py` |
| Отказ говорит, что делать, а не только что случилось | `clients/shared/i18n/ru.json` | `scripts/check-copy.mjs` |
| Снаружи «Поля», внутри ПДР | `docs/product/voice.md` | `scripts/check-copy.mjs` |
| Комментарии — только те, без которых не соберётся сборка | `docs/comments.md` | `scripts/check_comments.py` |
| Данные ваши: полная выгрузка аккаунта одним действием | `db/account/export.sql` | `account-export`, `scripts/check_openness.py` |
| `Idempotency-Key` обязателен на мутирующих запросах | нигде: HTTP-контура в дереве нет | намерение — вместе с областью API |
| Ошибки — problem+json с идентификатором запроса | нигде: обработчиков нет, есть только доменные коды | намерение; тексты кодов уже проверяет `scripts/check-copy.mjs` |
| Данные карт не проходят через бэкенд даже транзитом | нигде: платежей в дереве нет | намерение — вместе с областью BILL |
| Ни одной обязательной настройки | `docs/product/simplicity.md` | намерение: экранов нет ни одного, проверять нечего |
| Пустого экрана с призывом заполнить не существует | `docs/product/simplicity.md` | намерение: там же и по той же причине |
| Мы прослойка в помощи, а не продавец с товаром | `docs/adr/0017-a-layer-in-help-not-a-seller.md` | намерение: денег в дереве нет, проверять пока нечего |
| Факты считает код, смыслы отдаём модели | `docs/architecture/ai-sovereignty.md` | намерение: ни одного расчёта прогресса в дереве нет |

## Что из этого следует

**Двадцать семь постулатов из тридцати четырёх проверяются машиной.** Остальные
семь помечены намерением, и у каждого названа причина одного вида: кода, к
которому правило применяется, ещё нет.

Это и есть здоровое состояние фазы 0: правила, у которых есть предмет, уже
держатся проверками; правила, у которых предмета нет, честно названы
намерениями и получат тест вместе с предметом.

**Опасность здесь одна: намерение, которое тихо останется намерением после
появления предмета.** От неё защищает не этот документ, а порядок: правило,
вводимое задачей, вводится вместе с проверкой
([CONTRIBUTING.md](../../CONTRIBUTING.md)), и «правило без проверки не считается
введённым» — это про новые правила. Здешние семь строк — долг, оставшийся от
правил, введённых до кода.

## Как пополнять

Новый постулат — строка сюда в том же изменении, которым он принят. Либо ссылка
на проверку, либо слово «намерение» с причиной: третьего варианта проверка не
принимает, и это единственный способ не дать списку превратиться в лозунги.
