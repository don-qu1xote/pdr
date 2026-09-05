# Контекст scheduling: доступность, занятия, серии, отмены, окна бронирования.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Зависит от identity ровно одним заголовком — его публичным контрактом. Это
# видно и в CMake: pdr_identity отдаёт наружу только каталог contract.

add_library(pdr_scheduling STATIC
    src/scheduling/application/book_lesson.cpp
    src/scheduling/core/availability.cpp
    src/scheduling/core/lesson.cpp
    src/scheduling/core/overlap.cpp
    src/scheduling/core/recurrence.cpp
)
target_include_directories(pdr_scheduling PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_scheduling PUBLIC pdr_core pdr_events pdr_identity PRIVATE pdr_application)

# Адаптеры к Postgres — отдельной целью, как и в остальных контекстах: домен
# собирается и проверяется без userver, а хранение требует его целиком.
if(userver_FOUND)
    add_library(pdr_scheduling_postgres STATIC
        src/scheduling/infrastructure/postgres_availability_repository.cpp
        src/scheduling/infrastructure/postgres_lesson_repository.cpp
        src/scheduling/infrastructure/postgres_recurrence_repository.cpp
    )
    target_include_directories(pdr_scheduling_postgres
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_scheduling_postgres
                          PUBLIC pdr_scheduling pdr_postgres pdr_application userver::postgresql)
endif()

# Публичного контракта у scheduling пока нет: его никто не спрашивает
# синхронно, о занятиях узнают событием. Контракт заводится вместе с первым
# вызывающим, а не заранее.

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(
        tests/availability_test.cpp
        tests/book_lesson_test.cpp
        tests/lesson_state_test.cpp
        tests/overlap_test.cpp
        tests/recurrence_test.cpp
        tests/scheduling_contract.cpp
        tests/scheduling_fakes_contract_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_scheduling)

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
