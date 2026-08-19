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
find_package(userver QUIET COMPONENTS core postgresql)
if(userver_FOUND)
    add_library(pdr_billing_infrastructure STATIC
        src/billing/infrastructure/postgres_tariff_repository.cpp
        src/billing/infrastructure/tariff_repository_component.cpp
    )
    target_include_directories(pdr_billing_infrastructure
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_billing_infrastructure PUBLIC pdr_billing userver::postgresql)
else()
    message(STATUS "pdr: userver не найден, цель pdr_billing_infrastructure пропущена")
endif()

if(PDR_BUILD_TESTS)
    add_executable(pdr_billing_quote_test tests/quote_test.cpp)
    target_include_directories(pdr_billing_quote_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_billing_quote_test PRIVATE pdr_billing pdr_testing)
    add_test(NAME billing.quote COMMAND pdr_billing_quote_test)
endif()
