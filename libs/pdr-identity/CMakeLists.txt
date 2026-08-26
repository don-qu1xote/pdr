# Контекст identity: люди, роли, тенанты, опека, согласия, дееспособность.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Наружу видно РОВНО contract/identity/contract.hpp: каталог src приватный, и
# чужой модуль физически не может включить внутренность — не потому, что
# договорились, а потому, что её нет в его путях поиска.

add_library(pdr_identity STATIC
    src/identity/application/contract_service.cpp
    src/identity/application/note_sensitive_access.cpp
    src/identity/application/revoke_guardianship.cpp
    src/identity/core/access_record.cpp
    src/identity/core/age_status.cpp
    src/identity/core/birth_date.cpp
    src/identity/core/email.cpp
    src/identity/core/guardianship.cpp
    src/identity/core/membership.cpp
    src/identity/core/tenant.cpp
)
target_include_directories(pdr_identity
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_identity PUBLIC pdr_core pdr_events PRIVATE pdr_application)

# Адаптеры к Postgres собираются только там, где есть userver. Без него
# контекст остаётся собираемым и проверяемым целиком, кроме этого слоя.
if(userver_FOUND)
    # Журнал доступа пишется отсюда, и больше ниоткуда: таблицей
    # identity_access_log владеет этот контекст
    # (docs/architecture/context-map.md).
    add_library(pdr_identity_infrastructure STATIC
        src/identity/infrastructure/audit/postgres_access_log.cpp
    )
    target_include_directories(pdr_identity_infrastructure
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_identity_infrastructure
                          PUBLIC pdr_identity pdr_postgres userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ UNIT: домен и сценарии на фейках, всё в общей цели pdr_unit_tests.
    # Каталог tests тоже на путях включения: доменные билдеры (tests/builders/)
    # живут рядом со своим контекстом — платформенная оснастка зависеть от него
    # не имеет права.
    pdr_unit_test_sources(
        tests/access_log_test.cpp
        tests/age_status_test.cpp
        tests/guardianship_test.cpp
        tests/participants_test.cpp
        tests/revoke_guardianship_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_identity)
endif()
