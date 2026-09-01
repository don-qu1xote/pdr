# Контекст observability: продуктовые события — поток, обезличивание, срок жизни.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Наружу видно РОВНО contract/observability/contract.hpp: каталог src приватный,
# и чужой контекст физически не может включить внутренность.
#
# Технических метрик здесь нет и не будет: задержки, коды ответов и длины
# очередей — это штатные utils::statistics userver, и они не попадают в базу
# вовсе (docs/architecture/product-events.md).

add_library(pdr_observability STATIC
    src/observability/application/contract_service.cpp
    src/observability/core/product_event.cpp
)
target_include_directories(pdr_observability
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_observability PUBLIC pdr_core PRIVATE pdr_application)

# Адаптер, кладущий запись в observability_product_event, и компонент, который
# отдаёт контракт контекста процессу. Собирается только там, где есть userver:
# без него контекст остаётся собираемым и проверяемым целиком, кроме этого слоя.
if(userver_FOUND)
    add_library(pdr_observability_infrastructure STATIC
        src/observability/infrastructure/dynamic_config_stream_settings.cpp
        src/observability/infrastructure/postgres_product_event_stream.cpp
        src/observability/infrastructure/product_events_component.cpp
    )
    # PUBLIC, а не PRIVATE: цель проверок адаптеров включает заголовки отсюда.
    # Наружу это ничего не открывает — чужой контекст с этой целью не линкуется.
    target_include_directories(pdr_observability_infrastructure
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_observability_infrastructure
                          PUBLIC pdr_observability pdr_postgres pdr_infrastructure
                                 pdr_clock userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(tests/product_event_test.cpp)
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_observability)

    # Обезличивание проверяется не только тестом, но и сборкой: цель ниже ОБЯЗАНА
    # не собираться, потому что Value::Reference не принимает PersonId.
    add_executable(pdr_compile_fail_person_in_product_event EXCLUDE_FROM_ALL
                   tests/compile_fail/person_in_product_event.cpp)
    target_link_libraries(pdr_compile_fail_person_in_product_event PRIVATE pdr_observability)
    add_test(NAME unit.compile_fail.person_in_product_event
             COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                     --target pdr_compile_fail_person_in_product_event --config $<CONFIG>)
    set_tests_properties(unit.compile_fail.person_in_product_event PROPERTIES WILL_FAIL TRUE)

    # Адаптер потока на ЖИВОЙ базе: строка доходит до таблицы, вид значения не
    # теряется, а ограничение обезличивания отвергает то же, что домен.
    if(userver_FOUND)
        pdr_live_test_sources(tests/postgres_product_event_stream_test.cpp)
        pdr_live_test_libraries(pdr_observability_infrastructure)
    endif()
endif()
