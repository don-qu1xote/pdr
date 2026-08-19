# Контекст identity: люди, роли, тенанты, опека, согласия, дееспособность.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Наружу видно РОВНО contract/identity/contract.hpp: каталог src приватный, и
# чужой модуль физически не может включить внутренность — не потому, что
# договорились, а потому, что её нет в его путях поиска.

add_library(pdr_identity STATIC
    src/identity/application/contract_service.cpp
    src/identity/application/revoke_guardianship.cpp
    src/identity/core/guardianship.cpp
)
target_include_directories(pdr_identity
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_identity PUBLIC pdr_core pdr_events PRIVATE pdr_application)

# Слоя infrastructure здесь пока нет: адаптер к Postgres появится вместе с
# первой миграцией (область DB). Пустой каталог с .gitkeep этого не приблизит.

if(PDR_BUILD_TESTS)
    foreach(suite IN ITEMS guardianship revoke_guardianship)
        add_executable(pdr_identity_${suite}_test tests/${suite}_test.cpp)
        target_include_directories(pdr_identity_${suite}_test
                                   PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(pdr_identity_${suite}_test PRIVATE pdr_identity pdr_testing)
        add_test(NAME identity.${suite} COMMAND pdr_identity_${suite}_test)
    endforeach()
endif()
