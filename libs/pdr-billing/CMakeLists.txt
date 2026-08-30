# Контекст billing: счета, платежи, пакеты, автоплатежи, чеки.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Сюда переехал демонстрационный срез из PDR-ARCH-01 (тариф и цена пакета):
# карта контекстов обещала этот переезд, и правило живости требует, чтобы
# обещание выполнилось, а не осталось в разделе расхождений.

add_library(pdr_billing STATIC
    src/billing/application/contract_service.cpp
    src/billing/application/quote_lesson_package.cpp
    src/billing/core/lesson_package.cpp
    src/billing/core/tariff.cpp
)
target_include_directories(pdr_billing
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_billing PUBLIC pdr_core PRIVATE pdr_application)

# Адаптеры к Postgres собираются только там, где есть userver. Без него
# контекст остаётся собираемым и проверяемым целиком, кроме этого слоя.
if(userver_FOUND)
    # Компонента «репозиторий тарифов» больше нет: репозиторий живёт внутри
    # области арендатора и не переживает запрос, а компонент живёт всю жизнь
    # сервиса. Дверь к соединениям одна и общая — pdr_postgres.
    add_library(pdr_billing_infrastructure STATIC
        src/billing/infrastructure/postgres_tariff_repository.cpp
    )
    target_include_directories(pdr_billing_infrastructure
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_billing_infrastructure
                          PUBLIC pdr_billing pdr_postgres userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(tests/quote_test.cpp)
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_billing)
endif()
