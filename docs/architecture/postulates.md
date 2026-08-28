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
| Забытый арендатор даёт пустой ответ, а не чужие строки | `libs/pdr-core/src/infrastructure/db/tenant_context.cpp` | `scripts/check_isolation.py`, `libs/pdr-testing/include/pdr/testing/repository_contract.hpp` |
| Соединение берётся только с объявленным арендатором | `libs/pdr-core/src/infrastructure/db/tenant_context.hpp` | `scripts/check_layers.py`, `tenant_scope_without_tenant`, `tenant_scope_stashed` |
| Объявление арендатора не переживает транзакцию | `libs/pdr-core/src/infrastructure/db/tenant_context.cpp` | `scripts/check_rls.py`, `scripts/check_isolation.py` |
| Кто смотрел чужое — видно в журнале, и строку из него не убрать | `db/migrations/V005__access_log.sql` | `libs/pdr-identity/tests/access_log_test.cpp`, `scripts/check_isolation.py` |
| Пароль считается Argon2id, а не своей схемой | `libs/pdr-identity/src/identity/infrastructure/auth/argon2_password_hasher.cpp` | `libs/pdr-identity/tests/password_test.cpp`, `scripts/check_handmade.py` |
| Сессия серверная: отзыв действует немедленно | `db/migrations/V006__auth.sql` | `libs/pdr-identity/tests/sign_in_test.cpp`, `scripts/check_isolation.py` |
| Идентификатор сессии меняется при входе и смене пароля | `libs/pdr-identity/src/identity/application/sign_in.cpp` | `libs/pdr-identity/tests/sign_in_test.cpp` |
| Проверка сессии не знает, каким транспортом её принесли | `libs/pdr-identity/src/identity/application/authenticate_session.hpp` | `libs/pdr-identity/tests/session_transport_test.cpp` |
| Одноразовый токен хранится отпечатком, а не собой | `libs/pdr-identity/src/identity/application/ports/one_time_tokens.hpp` | `libs/pdr-identity/tests/invitation_test.cpp`, `scripts/check_isolation.py` |
| Счёт попыток входа живёт в базе, а не в памяти процесса | `libs/pdr-identity/src/identity/infrastructure/auth/postgres_login_attempts.hpp` | `libs/pdr-identity/tests/login_throttle_test.cpp` |
| Секреты берутся у криптографического источника, а не у mt19937 | `libs/pdr-core/src/application/ports/secret_generator.hpp` | `libs/pdr-core/tests/crypto_secret_generator_test.cpp` |
| Права решаются в одном месте, а не в хендлерах | `libs/pdr-identity/contract/identity/contract.hpp` | `libs/pdr-identity/tests/policy_registry_test.cpp` |
| У каждого действия есть политика, иначе не собирается прогон | `libs/pdr-identity/src/identity/application/policies/policy_set.cpp` | `libs/pdr-identity/tests/policy_registry_test.cpp` |
| Отказ несёт причину, а не «нельзя» | `libs/pdr-identity/contract/identity/contract.hpp` | `libs/pdr-identity/tests/policies_test.cpp` |
| Действие без политики запрещено и объявлено поломкой | `libs/pdr-identity/src/identity/application/ports/configuration_faults.hpp` | `libs/pdr-identity/tests/policy_registry_test.cpp` |
| Матрица прав собрана из кода, а не написана руками | `libs/pdr-identity/src/identity/application/policies/matrix.cpp` | `libs/pdr-identity/tests/permissions_matrix_test.cpp` |
| Супер-администратора не существует | `docs/architecture/permissions.md` | `libs/pdr-identity/tests/policies_test.cpp` |
| Единого «родитель видит всё» нет: доступ открывается по уровню за раз | `libs/pdr-identity/src/identity/core/guardian_scope.hpp` | `libs/pdr-identity/tests/policies_test.cpp`, `libs/pdr-identity/tests/guardian_access_test.cpp` |
| Записи занятий не открываются вместе с опекой | `libs/pdr-identity/src/identity/core/guardian_scope.cpp` | `libs/pdr-identity/tests/policies_test.cpp` |
| Отзыв доступа — строка с датой, а не удаление | `db/migrations/V007__guardian_access.sql` | `libs/pdr-identity/tests/guardian_access_test.cpp`, `scripts/check_isolation.py` |
| Совершеннолетие даёт срок на решение, а не мгновенный обрыв | `libs/pdr-identity/src/identity/core/guardian_access.cpp` | `libs/pdr-identity/tests/guardian_access_test.cpp` |
| Отказ опекуну попадает в журнал наравне с просмотром | `libs/pdr-identity/src/identity/application/contract_service.cpp` | `libs/pdr-identity/tests/guardian_access_test.cpp` |
| Развилки при регистрации нет: подбор выключен у всех по умолчанию | `libs/pdr-identity/src/identity/core/practice.hpp` | `libs/pdr-identity/tests/onboarding_test.cpp` |
| Один человек на площадке, сколько угодно практик | `libs/pdr-identity/src/identity/core/account.hpp` | `libs/pdr-identity/tests/onboarding_test.cpp`, `scripts/check_isolation.py` |
| Репетитор не видит ничего о занятиях ученика у других | `db/migrations/V008__practice_and_accounts.sql` | `scripts/check_isolation.py`, `libs/pdr-identity/tests/onboarding_test.cpp` |
| Учебное число не пересекает границу практики | `scripts/migration_model.py` | `scripts/check_rls.py` |
| Повтор приглашения не шлёт второго письма | `libs/pdr-identity/src/identity/application/invite_participant.hpp` | `libs/pdr-identity/tests/onboarding_test.cpp` |
| Практику можно выгрузить целиком и удалить целиком | `db/account/delete.sql` | `scripts/check_openness.py`, `make account-export` |
| Права по возрасту вычисляются, а не выдаются по заявке | `libs/pdr-identity/src/identity/core/capabilities.hpp` | `libs/pdr-identity/tests/capabilities_test.cpp` |
| Возрастные пороги — значения конфига, а не константы | `configs/dynamic/registry.yaml` | `scripts/check_dynamic_configs.py`, `libs/pdr-identity/tests/capabilities_test.cpp` |
| Опекун узнаёт о самостоятельном поступке подопечного всегда | `libs/pdr-identity/src/identity/application/notify_guardian_of_act.cpp` | `scripts/check_guardian_notice.py`, `libs/pdr-notifications/tests/deliver_domain_events_test.cpp` |
| Текст отзыва опекуну не показывают: в событии для него нет места | `libs/pdr-events/include/events/identity/ward_acted_alone.hpp` | `scripts/check_guardian_notice.py` |
| Автоплатёж с чужой карты ученику не даётся ни в каком возрасте | `libs/pdr-identity/src/identity/application/policies/billing_policy.cpp` | `libs/pdr-identity/tests/capabilities_test.cpp` |
| Ученик читает журнал доступов к себе, опекун — нет ни при каком уровне | `libs/pdr-identity/src/identity/application/show_access_journal.hpp` | `libs/pdr-identity/tests/guardian_access_test.cpp` |
| Взрослый ученик без опекуна проходит весь путь: опека нигде не обязательна | `libs/pdr-identity/src/identity/application/policies/subject_builder.cpp` | `scripts/check_adult_student.py`, `libs/pdr-identity/tests/adult_student_test.cpp` |
| Опека и наблюдение — один механизм с разными основаниями | `libs/pdr-identity/src/identity/core/guardian_consent.hpp` | `libs/pdr-identity/tests/adult_student_test.cpp` |
| Деньги не дают права смотреть: плательщику открыты только счета | `db/migrations/V009__consent_basis.sql` | `libs/pdr-identity/tests/adult_student_test.cpp`, `scripts/check_isolation.py` |
| Возраст при регистрации заявительный: документов продукт не просит | `libs/pdr-identity/src/identity/core/birth_date.hpp` | `scripts/check_adult_student.py` |
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
