# Контекст scheduling: доступность, занятия, серии, отмены, окна бронирования.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Зависит от identity ровно одним заголовком — его публичным контрактом. Это
# видно и в CMake: pdr_identity отдаёт наружу только каталог contract.

add_library(pdr_scheduling STATIC
    src/scheduling/application/book_lesson.cpp
    src/scheduling/application/cancel_lesson.cpp
    src/scheduling/application/create_series.cpp
    src/scheduling/application/get_availability.cpp
    src/scheduling/application/get_lesson.cpp
    src/scheduling/application/list_lessons.cpp
    src/scheduling/application/set_availability.cpp
    src/scheduling/core/availability.cpp
    src/scheduling/core/cancellation_policy.cpp
    src/scheduling/core/lesson.cpp
    src/scheduling/core/lesson_history.cpp
    src/scheduling/core/overlap.cpp
    src/scheduling/core/recurrence.cpp
)
target_include_directories(pdr_scheduling PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_scheduling PUBLIC pdr_core pdr_events pdr_identity PRIVATE pdr_application)

# Адаптеры к Postgres — отдельной целью, как и в остальных контекстах: домен
# собирается и проверяется без userver, а хранение требует его целиком.
if(userver_FOUND)
    add_library(pdr_scheduling_postgres STATIC
        src/scheduling/infrastructure/dynamic_config_cancellation_policies.cpp
        src/scheduling/infrastructure/postgres_availability_repository.cpp
        src/scheduling/infrastructure/postgres_lesson_history.cpp
        src/scheduling/infrastructure/postgres_lesson_repository.cpp
        src/scheduling/infrastructure/postgres_recurrence_repository.cpp
    )
    target_include_directories(pdr_scheduling_postgres
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_scheduling_postgres
                          PUBLIC pdr_scheduling pdr_postgres pdr_application pdr_observe
                                 pdr_dynamic_configs userver::postgresql)

    # РУЧКИ РАСПИСАНИЯ — отдельной целью, как и у identity: форма запроса, типы
    # контракта и опознание пришедшего нужны только им, а домену — нет.
    #
    # Ручки живут ЗДЕСЬ, а не в services/: процесс собирает контексты и не лезет
    # в них, а «какое действие спрашивать у политики» — знание расписания. В
    # сервисе от них остаётся строка в списке компонентов и строка в конфиге.
    add_library(pdr_scheduling_http STATIC
        src/scheduling/infrastructure/http/api_mapping.cpp
        src/scheduling/infrastructure/http/arguments.cpp
        src/scheduling/infrastructure/http/availability_operations.cpp
        src/scheduling/infrastructure/http/lesson_operations.cpp
        src/scheduling/infrastructure/http/parts.cpp
        src/scheduling/infrastructure/http/series_operation.cpp
    )
    target_include_directories(pdr_scheduling_http PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_scheduling_http
                          PUBLIC pdr_scheduling_postgres pdr_api pdr_http pdr_http_postgres
                                 pdr_identity_infrastructure)
endif()

# Публичного контракта у scheduling пока нет: его никто не спрашивает
# синхронно, о занятиях узнают событием. Контракт заводится вместе с первым
# вызывающим, а не заранее.

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(
        tests/availability_test.cpp
        tests/book_lesson_test.cpp
        tests/list_lessons_test.cpp
        tests/get_lesson_test.cpp
        tests/cancel_lesson_test.cpp
        tests/cancellation_test.cpp
        tests/create_series_test.cpp
        tests/set_availability_test.cpp
        tests/lesson_state_test.cpp
        tests/overlap_test.cpp
        tests/recurrence_test.cpp
        tests/scheduling_contract.cpp
        tests/scheduling_fakes_contract_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_scheduling)

    # УРОВЕНЬ ADAPTER: разбор динамического значения. Ни компилятор, ни фейк не
    # скажут, что доля в полтораста процентов доедет до домена, — это говорит
    # только настоящий разборщик штатного dynamic_config.
    if(userver_FOUND)
        pdr_adapter_test_sources(tests/dynamic_config_cancellation_policies_test.cpp)
        pdr_adapter_test_libraries(pdr_scheduling_postgres pdr_testing)
    endif()

    # УРОВЕНЬ LIVE: те же утверждения contract-набора, но против настоящих
    # адаптеров и живой базы. Схему живой набор берёт из db/migrations, а не из
    # своей копии.
    #
    # Рядом — два утверждения, которых в наборе нет и быть не может: гонка за
    # слот (фейк живёт в одном потоке и чередования не знает) и план запроса
    # (плана у фейка нет вовсе).
    if(userver_FOUND)
        pdr_live_test_sources(tests/scheduling_contract.cpp
                              tests/postgres_scheduling_contract_test.cpp
                              tests/postgres_lesson_race_test.cpp
                              tests/postgres_scheduling_plan_test.cpp)
        pdr_live_test_includes(src tests)
        pdr_live_test_libraries(pdr_scheduling_postgres)
    endif()

    # Граница контекста проверяется не только линтером, но и сборкой: цель ниже
    # ОБЯЗАНА не собираться, потому что внутренностей identity нет в путях
    # поиска этого модуля.
    add_executable(pdr_compile_fail_identity_internals EXCLUDE_FROM_ALL
                   tests/compile_fail/identity_internals.cpp)
    target_include_directories(pdr_compile_fail_identity_internals
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_compile_fail_identity_internals PRIVATE pdr_scheduling)
    add_test(NAME unit.compile_fail.identity_internals
             COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                     --target pdr_compile_fail_identity_internals --config $<CONFIG>)
    set_tests_properties(unit.compile_fail.identity_internals PROPERTIES WILL_FAIL TRUE)
endif()
