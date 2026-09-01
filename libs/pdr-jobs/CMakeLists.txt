# Контекст jobs: одиночные периодические задания на штатном DistLock.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md,
# устройство механизма — docs/architecture/jobs.md.
#
# Технический контекст: доменных правил здесь нет, есть механизм, которым
# пользуются доменные. Наружу видно ровно одно — contract/jobs/contract.hpp:
# что запланировать и что произвести.

add_library(pdr_jobs STATIC
    src/jobs/application/run_periodic_job.cpp
    src/jobs/core/job_name.cpp
    src/jobs/core/job_settings.cpp
    src/jobs/core/run_record.cpp
    src/jobs/core/system_tenant.cpp
)
target_include_directories(pdr_jobs
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_jobs PUBLIC pdr_core PRIVATE pdr_application)

# Блокировка, журнал в базе, настройки из динамического конфига и компонент
# сервиса требуют userver — как и остальные адаптеры в дереве. Без него контекст
# остаётся собираемым и проверяемым целиком, кроме этого слоя, а живую часть
# механизма проверяет scripts/check_jobs.py на настоящей базе.
if(userver_FOUND)
    add_library(pdr_jobs_infrastructure STATIC
        src/jobs/infrastructure/dynamic_config_job_settings.cpp
        src/jobs/infrastructure/periodic_job_component.cpp
        src/jobs/infrastructure/postgres_job_journal.cpp
        src/jobs/infrastructure/postgres_job_ledger.cpp
    )
    target_include_directories(pdr_jobs_infrastructure
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # pdr_infrastructure — настоящие часы: механизм заданий спрашивает время у
    # порта, а реализацию порта берёт отсюда.
    target_link_libraries(pdr_jobs_infrastructure
                          PUBLIC pdr_jobs pdr_postgres pdr_infrastructure pdr_clock
                                 userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(
        tests/job_settings_test.cpp
        tests/run_periodic_job_test.cpp
    )
    pdr_unit_test_includes(src)
    pdr_unit_test_libraries(pdr_jobs)

    # Адаптер конфига проверяется против штатных заглушек userver: умолчание из
    # кода без источника, применение без пересоздания адаптера, отказ значению
    # вне диапазона и журнал «было → стало». Цель собирается только там, где
    # userver есть, — то есть в ярусе 3 CI.
    if(userver_FOUND)
        pdr_adapter_test_sources(tests/dynamic_config_job_settings_test.cpp)
        pdr_adapter_test_libraries(pdr_jobs_infrastructure)
    endif()
endif()
