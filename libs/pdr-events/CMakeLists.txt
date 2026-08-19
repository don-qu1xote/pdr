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
    foreach(suite IN ITEMS bus registry)
        add_executable(pdr_events_${suite}_test tests/${suite}_test.cpp)
        target_link_libraries(pdr_events_${suite}_test PRIVATE pdr_events pdr_testing)
        add_test(NAME events.${suite} COMMAND pdr_events_${suite}_test)
    endforeach()
endif()
