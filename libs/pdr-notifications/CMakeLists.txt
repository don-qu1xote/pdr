# Контекст notifications: доставка почтой и push поверх outbox.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Публичного контракта у него по-прежнему нет: соседи его не спрашивают ни о
# чём. То, что он слушает события чужой транзакции, контрактом не становится —
# издатель видит платформенный порт `events::Listeners`, а не этот контекст, и
# под каким именем поднят слушатель, решает статический конфиг процесса.

add_library(pdr_notifications STATIC
    src/notifications/application/deliver_domain_events.cpp
    src/notifications/application/dispatch_outbox.cpp
    src/notifications/core/delivery.cpp
    src/notifications/core/outbox_entry.cpp
    src/notifications/core/retry.cpp
)
target_include_directories(pdr_notifications PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_notifications PUBLIC pdr_core pdr_events PRIVATE pdr_application)

# Адаптеры и компоненты — отдельной целью, как и в остальных контекстах: домен
# со сценариями собирается и проверяется без userver, а очередь в Postgres
# требует его целиком.
if(userver_FOUND)
    add_library(pdr_notifications_postgres STATIC
        src/notifications/infrastructure/dynamic_config_retry_policies.cpp
        src/notifications/infrastructure/logging_notification_sender.cpp
        src/notifications/infrastructure/outbox_dispatcher.cpp
        src/notifications/infrastructure/postgres_outbox.cpp
        src/notifications/infrastructure/postgres_outbox_queue.cpp
        src/notifications/infrastructure/transactional_outbox.cpp
    )
    target_include_directories(pdr_notifications_postgres
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_notifications_postgres
                          PUBLIC pdr_notifications pdr_postgres pdr_application pdr_observe
                                 pdr_dynamic_configs userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(
        tests/deliver_domain_events_test.cpp
        tests/dispatch_outbox_test.cpp
        tests/retry_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_notifications)

    # УРОВЕНЬ ADAPTER: разбор настроек очереди. Что предел попыток в ноль не
    # доедет до домена, не скажет ни компилятор, ни фейк — это говорит только
    # настоящий разборщик штатного dynamic_config.
    if(userver_FOUND)
        pdr_adapter_test_sources(tests/dynamic_config_retry_policies_test.cpp)
        pdr_adapter_test_libraries(pdr_notifications_postgres pdr_testing)
    endif()

    # УРОВЕНЬ LIVE: то, чего у фейка нет и быть не может. Одновременность бывает
    # только в базе — на фейке `skip locked` был бы зелёным и без самой
    # директивы; изоляция бывает только в политике; ограничение причины отказа —
    # только в схеме.
    if(userver_FOUND)
        pdr_live_test_sources(tests/postgres_outbox_live_test.cpp)
        pdr_live_test_includes(src tests)
        pdr_live_test_libraries(pdr_notifications_postgres)
    endif()
endif()
