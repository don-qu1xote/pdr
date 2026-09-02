# ЗНАЧЕНИЯ ДИНАМИЧЕСКИХ КОНФИГОВ — ИЗ РЕЕСТРА, А НЕ ИЗ ГОЛОВЫ.
#
# Здесь нет ни одного написанного руками типа. Структуры, разборщики и сами
# объекты `dynamic_config::Key` — вместе с умолчаниями и пределами — порождает
# штатный chaotic из configs/dynamic/registry.yaml на этапе сборки (ADR-0013).
#
# ЧТО ЭТИМ ЗАКРЫВАЕТСЯ. Раньше схема в реестре и структура в коде были двумя
# разными текстами про одно и то же, и за их расхождением следила проверка. Тип,
# порождённый из схемы, разойтись с ней не может: поля, которого в схеме нет, нет
# и в структуре, а умолчание в коде — то же самое умолчание из реестра, потому
# что источник один. Проверка похудела ровно на это (scripts/check_dynamic_configs.py).
#
# ПОРОЖДЁННОЕ НЕ КОММИТИТСЯ: оно живёт в каталоге сборки и появляется заново у
# каждого, кто собирает.
#
# Без userver порождать нечем: chaotic — его часть.
if(NOT userver_FOUND)
    return()
endif()

# РАСКЛАДКА РЕЕСТРА. Штатное порождение читает один файл на одну величину и берёт
# её имя из имени файла; реестр же остаётся ОДНИМ файлом — его читает человек, и
# на него ссылаются проверки и документы. Раскладка ничего не порождает: она
# режет реестр по записям (scripts/dynamic_config_schemas.py).
#
# На этапе конфигурации, а не сборки: список файлов нужен уже сейчас — по нему
# штатная функция заводит цели.
set(PDR_CONFIG_SCHEMAS "${CMAKE_CURRENT_BINARY_DIR}/schemas")
set(PDR_CONFIG_REGISTRY "${CMAKE_SOURCE_DIR}/configs/dynamic/registry.yaml")

execute_process(
    COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/dynamic_config_schemas.py"
            --root "${CMAKE_SOURCE_DIR}" --out "${PDR_CONFIG_SCHEMAS}"
    RESULT_VARIABLE PDR_CONFIG_LAID
    OUTPUT_VARIABLE PDR_CONFIG_LAID_OUT
    ERROR_VARIABLE PDR_CONFIG_LAID_ERR
)
if(NOT PDR_CONFIG_LAID EQUAL 0)
    message(FATAL_ERROR "pdr: реестр динамических значений не разложился: ${PDR_CONFIG_LAID_ERR}")
endif()
message(STATUS "pdr: ${PDR_CONFIG_LAID_OUT}")

# Правка реестра обязана перезапустить конфигурацию: иначе раскладка отстанет от
# него, а порождение — от раскладки, и сборка будет собирать вчерашние пределы.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND
             PROPERTY CMAKE_CONFIGURE_DEPENDS "${PDR_CONFIG_REGISTRY}")

# Где порождение ищет заголовок сопряжения для `x-usrv-cpp-type`. Ровно один
# такой заголовок в дереве — доменные настройки задания: связь между полями
# схемой не выражается и остаётся у домена.
set_property(GLOBAL APPEND PROPERTY userver_chaotic_extra_args
             "-I" "${CMAKE_SOURCE_DIR}/libs/pdr-jobs/src/jobs/infrastructure/chaotic-io")

userver_target_generate_chaotic_dynamic_configs(pdr_dynamic_configs "${PDR_CONFIG_SCHEMAS}/*.yaml")

# Доменный тип настроек задания виден в порождённой структуре, значит виден и
# всякому, кто её включает. `src` контекста jobs — не публичный каталог, и
# пускают сюда именно за этим одним заголовком: сопряжение живёт в модуле,
# которому принадлежит тип, а не рядом с порождением.
target_include_directories(pdr_dynamic_configs
    PUBLIC "${CMAKE_SOURCE_DIR}/libs/pdr-jobs/src/jobs/infrastructure/chaotic-io"
           "${CMAKE_SOURCE_DIR}/libs/pdr-jobs/src"
)
target_link_libraries(pdr_dynamic_configs pdr_jobs)
