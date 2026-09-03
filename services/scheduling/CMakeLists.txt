# ПЕРВЫЙ ПРОЦЕСС ПРОЕКТА.
#
# До него всё, что написано в libs/, ни разу не запускалось: домен, схема и
# механизмы в поднятом сервисе не нуждаются. Плата за это — долги, куски,
# дописанные до половины и ждущие процесса
# (docs/architecture/first-service.md). Здесь они начинают закрываться.
#
# Доменной логики расписания тут нет ни строчки — она в PDR-SCHED-12. Здесь
# каркас: точка входа, полный набор компонентов, две ручки состояния, задание,
# продуктовый поток и одна работающая ручка входа сквозь все слои.
#
# Процесс СОБИРАЕТ контексты и не лезет в них: ему видны публичные контракты и
# сборочный слой infrastructure, а core и application чужого контекста — нет.
# Правило проверяет scripts/check_layers.py.
#
# Без userver сервис не собирается, и это не оговорка: сервис — это и есть
# userver. Модули libs/ при этом собираются и проверяются без него.
if(NOT userver_FOUND)
    return()
endif()

# Вход считает Argon2id, и подменять его «чем-нибудь попроще» запрещено
# (ADR-0013). Нет библиотеки — нет и сервиса: собрать его без счёта паролей
# значит собрать процесс, в который нельзя войти.
if(NOT TARGET pdr_identity_http)
    message(STATUS "pdr: libargon2 не найдена, сервис расписания пропущен")
    return()
endif()

add_executable(pdr_scheduling_service
    src/scheduling_service/main.cpp
    src/scheduling_service/authorized_route.cpp
    src/scheduling_service/health_handler.cpp
    src/scheduling_service/heartbeat_job.cpp
    src/scheduling_service/openapi_handler.cpp
    src/scheduling_service/readiness_handler.cpp
    src/scheduling_service/secrets_guard.cpp
)
target_include_directories(pdr_scheduling_service PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_scheduling_service
    PRIVATE pdr_core
            pdr_application
            pdr_infrastructure
            pdr_postgres
            pdr_secrets
            pdr_crypto
            pdr_http
            pdr_http_postgres
            pdr_identity_http
            pdr_jobs_infrastructure
            pdr_observability_infrastructure
            userver::postgresql
)

# КОНТУР pytest_userver. Сервис поднимает и гасит ПЛАГИН, а не conftest.py:
# свой стенд запрещён и проверяется scripts/check_testsuite.py.
#
# Функция сама находит configs/static_config.yaml, configs/dynamic_config_fallback.json,
# configs/secdist.json и tests/conftest.py — раскладка стандартная намеренно.
if(PDR_BUILD_TESTS AND USERVER_FEATURE_TESTSUITE)
    # Имя исполняемого файла называется явно: штатная функция собирает его из
    # имени ПРОЕКТА, а проект у нас один на всё дерево — иначе контур искал бы
    # файл «pdr».
    # SQL_LIBRARY включает штатный sql_coverage: контур считает покрытие ЗАПРОСОВ,
    # а не строк кода. Список запросов он берёт из порождённого sql_files.py, а
    # факт выполнения — из штатной точки sql_statement, которую ставит драйвер
    # Postgres. Запрос без единого выполнения роняет прогон; какие из них не
    # выполняются осознанно и почему — названо поимённо в tests/conftest.py.
    userver_testsuite_add_simple(
        SERVICE_TARGET pdr_scheduling_service
        SQL_LIBRARY pdr_sql
        PYTEST_ARGS "--service-binary=${CMAKE_CURRENT_BINARY_DIR}/pdr_scheduling_service"
                    "-o" "cache_dir=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/pytest_cache"
    )

    # СВЕРКА С КОНТРАКТОМ — отдельным набором и на том же процессе.
    #
    # Отдельным не ради изоляции, а ради предмета: сценарии проверяют, что сервис
    # делает что нужно, а этот — что он отвечает так, как обещано клиенту.
    # Спецификация, которую никто не сверяет с работающим процессом, устаревает
    # за две недели, и это происходит всегда.
    userver_testsuite_add_simple(
        SERVICE_TARGET pdr_scheduling_service
        TEST_SUFFIX openapi
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tests/contract/openapi_conformance"
        REQUIREMENTS "${CMAKE_SOURCE_DIR}/tests/contract/openapi_conformance/requirements.txt"
        PYTEST_ARGS "--service-binary=${CMAKE_CURRENT_BINARY_DIR}/pdr_scheduling_service"
                    "--service-config=${CMAKE_CURRENT_SOURCE_DIR}/configs/static_config.yaml"
                    "--service-source-dir=${CMAKE_CURRENT_SOURCE_DIR}"
                    "--service-secdist=${CMAKE_CURRENT_SOURCE_DIR}/configs/secdist.json"
                    "--config-fallback=${CMAKE_CURRENT_SOURCE_DIR}/configs/dynamic_config_fallback.json"
                    "--service-config-vars=${CMAKE_CURRENT_SOURCE_DIR}/configs/config_vars.yaml"
                    "-p" "no:cacheprovider"
    )
endif()
