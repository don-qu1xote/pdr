# Контекст notifications: доставка почтой и push поверх outbox.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Единственный контекст в этой фазе, который никого не спрашивает и ничего не
# публикует: он только слушает. Поэтому у него нет публичного контракта — и
# заводить пустой незачем.

add_library(pdr_notifications STATIC
    src/notifications/application/deliver_domain_events.cpp
    src/notifications/core/delivery.cpp
)
target_include_directories(pdr_notifications PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_notifications PUBLIC pdr_core pdr_events PRIVATE pdr_application)

if(PDR_BUILD_TESTS)
    add_executable(pdr_notifications_deliver_test tests/deliver_domain_events_test.cpp)
    target_include_directories(pdr_notifications_deliver_test
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_notifications_deliver_test PRIVATE pdr_notifications pdr_testing)
    add_test(NAME notifications.deliver COMMAND pdr_notifications_deliver_test)
endif()
