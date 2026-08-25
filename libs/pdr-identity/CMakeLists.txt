# Контекст identity: люди, роли, тенанты, опека, согласия, дееспособность.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Наружу видно РОВНО contract/identity/contract.hpp: каталог src приватный, и
# чужой модуль физически не может включить внутренность — не потому, что
# договорились, а потому, что её нет в его путях поиска.

add_library(pdr_identity STATIC
    src/identity/application/contract_service.cpp
    src/identity/application/revoke_guardianship.cpp
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

# Слоя infrastructure здесь пока нет: адаптер к Postgres появится вместе с
# первой миграцией (область DB). Пустой каталог с .gitkeep этого не приблизит.

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ UNIT: домен и сценарии на фейках, всё в общей цели pdr_unit_tests.
    # Каталог tests тоже на путях включения: доменные билдеры (tests/builders/)
    # живут рядом со своим контекстом — платформенная оснастка зависеть от него
    # не имеет права.
    pdr_unit_test_sources(
        tests/age_status_test.cpp
        tests/guardianship_test.cpp
        tests/participants_test.cpp
        tests/revoke_guardianship_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_identity)
endif()
