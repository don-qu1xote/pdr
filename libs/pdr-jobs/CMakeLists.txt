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
find_package(userver QUIET COMPONENTS core postgresql)
if(userver_FOUND)
    add_library(pdr_jobs_infrastructure STATIC
        src/jobs/infrastructure/dynamic_config_job_settings.cpp
        src/jobs/infrastructure/periodic_job_component.cpp
        src/jobs/infrastructure/postgres_job_journal.cpp
        src/jobs/infrastructure/postgres_job_ledger.cpp
    )
    target_include_directories(pdr_jobs_infrastructure
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_jobs_infrastructure
                          PUBLIC pdr_jobs pdr_postgres userver::postgresql)
else()
    message(STATUS "pdr: userver не найден, цель pdr_jobs_infrastructure пропущена")
endif()

if(PDR_BUILD_TESTS)
    foreach(suite IN ITEMS job_settings run_periodic_job)
        add_executable(pdr_jobs_${suite}_test tests/${suite}_test.cpp)
        target_include_directories(pdr_jobs_${suite}_test
                                   PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(pdr_jobs_${suite}_test PRIVATE pdr_jobs pdr_testing)
        add_test(NAME jobs.${suite} COMMAND pdr_jobs_${suite}_test)
    endforeach()
endif()
