# Общий реестр типов доменных событий.
#
# Типы событий лежат ЗДЕСЬ, а не у издателя: иначе подписчик включал бы
# заголовок чужого контекста, и связь, которую мы разрываем, вернулась бы
# через include. Общего заголовка «все события всех контекстов» нет и не будет:
# один тип — один заголовок, включают только нужный.

add_library(pdr_events STATIC
    src/in_memory_bus.cpp
)
target_include_directories(pdr_events PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(pdr_events PUBLIC pdr_core)

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(
        tests/bus_test.cpp
        tests/registry_test.cpp
    )
    pdr_unit_test_libraries(pdr_events)
endif()
