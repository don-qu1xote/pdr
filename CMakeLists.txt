# Корень сборки ПДР.
#
# Здесь заводится ровно то, что не принадлежит ни одной библиотеке: стандарт
# языка, оснастка тестов и ГРАНИЦЫ, которые проверяет сама сборка.
cmake_minimum_required(VERSION 3.25)

project(pdr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json нужен clangd и clang-tidy, поэтому включён всегда.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Тип сборки" FORCE)
endif()

# Тесты собираются по умолчанию: unit-тесты ядра и сценариев идут за
# миллисекунды и не требуют ни базы, ни докера, ни сети.
option(PDR_BUILD_TESTS "Собирать тесты" ON)
if(PDR_BUILD_TESTS)
    enable_testing()
endif()

# Ссылка, из которой линкуется цель, — это её зависимость, чем бы её ни называли
# в разговоре. Функция читает ИТОГОВЫЕ зависимости и роняет конфигурацию, если
# запрещённая среди них нашлась. Вызывается отложенно: к концу конфигурации видны
# и те зависимости, которые дописали в другом каталоге.
function(pdr_forbid_links target reason)
    set(forbidden ${ARGN})
    foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(links ${target} ${property})
        if(NOT links)
            continue()
        endif()
        foreach(link IN LISTS links)
            foreach(name IN LISTS forbidden)
                if(link MATCHES "(^|::|/|-|_)${name}(-|_|$|\\.)" OR link STREQUAL "${name}")
                    message(FATAL_ERROR
                            "Цель ${target} не может зависеть от ${link} "
                            "(свойство ${property}). ${reason}")
                endif()
            endforeach()
        endforeach()
    endforeach()
endfunction()

# ОСНАСТКА ТЕСТОВ.
#
# gtest берётся готовым: свой фреймворк тестов не пишется (PDR-TST-01). Сначала
# ищется установленный, и только если его нет — скачивается закреплённая версия.
# Версия и её сумма записаны здесь, а не «последняя с гитхаба»: прогон обязан
# быть одинаковым у всех.
option(PDR_FETCH_GTEST "Скачать gtest, если в системе его нет" ON)
set(PDR_GTEST_VERSION "1.15.2" CACHE STRING "Версия gtest")
set(PDR_GTEST_SHA256 "7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926"
    CACHE STRING "Сумма архива gtest")

if(PDR_BUILD_TESTS)
    find_package(GTest QUIET CONFIG)
    if(NOT TARGET GTest::gtest_main AND PDR_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL "https://github.com/google/googletest/archive/refs/tags/v${PDR_GTEST_VERSION}.tar.gz"
            URL_HASH "SHA256=${PDR_GTEST_SHA256}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
                "gtest не найден и скачивать его запрещено (PDR_FETCH_GTEST=OFF). "
                "Поставьте googletest или разрешите загрузку.")
    endif()
endif()

# ТРИ УРОВНЯ ПИРАМИДЫ (docs/testing.md) — три набора целей:
#
#   pdr_unit_tests      core и application на фейках. Ни базы, ни докера, ни сети.
#   pdr_contract_tests  ОДИН набор проверок на фейке и на настоящем адаптере.
#   интеграционные      поднятая установка: make test-isolation, make test-jobs.
#
# Модули не заводят своих тестовых исполняемых файлов: они добавляют исходники
# сюда. Один прогон вместо четырнадцати — это и есть «секунды на весь unit».
function(pdr_unit_test_sources)
    foreach(source IN LISTS ARGN)
        set_property(GLOBAL APPEND PROPERTY PDR_UNIT_TEST_SOURCES
                     "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endforeach()
endfunction()

function(pdr_unit_test_includes)
    foreach(directory IN LISTS ARGN)
        set_property(GLOBAL APPEND PROPERTY PDR_UNIT_TEST_INCLUDES
                     "${CMAKE_CURRENT_SOURCE_DIR}/${directory}")
    endforeach()
endfunction()

function(pdr_unit_test_libraries)
    set_property(GLOBAL APPEND PROPERTY PDR_UNIT_TEST_LIBRARIES ${ARGN})
endfunction()

# Проверки адаптеров против ШТАТНЫХ заглушек userver: dynamic_config::StorageMock,
# перехват журнала. Ни сервиса, ни базы, ни докера — но и без userver их не
# собрать, поэтому цель заводится только там, где он найден.
function(pdr_adapter_test_sources)
    foreach(source IN LISTS ARGN)
        set_property(GLOBAL APPEND PROPERTY PDR_ADAPTER_TEST_SOURCES
                     "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endforeach()
endfunction()

function(pdr_adapter_test_libraries)
    set_property(GLOBAL APPEND PROPERTY PDR_ADAPTER_TEST_LIBRARIES ${ARGN})
endfunction()

# userver ищется РОВНО ОДИН РАЗ и здесь.
#
# Это не стиль, а требование: импортированные цели, которые создаёт
# find_package, видны только в своём каталоге и ниже, а признаки найденности
# зависимостей (Boost_FOUND, Threads_FOUND) кэшируются глобально. Второй вызов в
# другом каталоге пропускает find_dependency как «уже найдено», и ссылки вида
# Boost::stacktrace_backtrace в интерфейсе userver::core оказываются битыми —
# сборка падает на этапе генерации, а не конфигурации, и сообщение указывает на
# чужой файл. Проверено в контейнере с настоящим userver.
#
# Модули не ищут userver сами: они смотрят на userver_FOUND.
find_package(userver QUIET COMPONENTS core postgresql)
if(userver_FOUND)
    message(STATUS "pdr: userver найден, слой адаптеров собирается")
else()
    message(STATUS "pdr: userver не найден, цели адаптеров пропущены")
endif()

# Подкаталог участвует в сборке, как только в нём появляется свой CMakeLists.txt.
# Пока их нет, конфигурация проходит и не собирает ничего.
function(pdr_add_subprojects root)
    file(GLOB entries CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${root}/*")
    list(SORT entries)
    foreach(entry IN LISTS entries)
        if(EXISTS "${entry}/CMakeLists.txt")
            get_filename_component(name "${entry}" NAME)
            add_subdirectory("${root}/${name}")
        endif()
    endforeach()
endfunction()

pdr_add_subprojects(libs)
pdr_add_subprojects(services)

if(PDR_BUILD_TESTS)
    get_property(unit_sources GLOBAL PROPERTY PDR_UNIT_TEST_SOURCES)
    get_property(unit_includes GLOBAL PROPERTY PDR_UNIT_TEST_INCLUDES)
    get_property(unit_libraries GLOBAL PROPERTY PDR_UNIT_TEST_LIBRARIES)

    if(unit_sources)
        add_executable(pdr_unit_tests ${unit_sources})
        target_include_directories(pdr_unit_tests PRIVATE ${unit_includes})
        target_link_libraries(pdr_unit_tests PRIVATE ${unit_libraries} GTest::gtest_main)
        add_test(NAME unit COMMAND pdr_unit_tests)

        # Путь к дереву нужен РОВНО ОДНОМУ тесту — сверке матрицы прав с
        # docs/architecture/permissions.md. Матрица собирается из самих политик,
        # и написанная руками она разошлась бы с кодом на первой правке.
        # Перезаписать файл: make permissions-lock.
        target_compile_definitions(pdr_unit_tests
                                   PRIVATE PDR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                                           PDR_BINARY_DIR="${CMAKE_BINARY_DIR}")

        # ГЛАВНАЯ ГРАНИЦА ПИРАМИДЫ, и она проверяется сборкой, а не обещанием:
        # у unit-прогона нет ни адаптеров, ни драйвера базы, ни userver. Значит,
        # «а я тут быстренько сходил в Postgres» не компилируется — вместо того
        # чтобы медленно работать и падать в CI.
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pdr_forbid_links
                       pdr_unit_tests
                       "Unit-прогон идёт без базы и без докера: адаптеры проверяет contract-набор (docs/testing.md)."
                       infrastructure postgres userver pqxx PostgreSQL libpq)
    endif()

    get_property(adapter_sources GLOBAL PROPERTY PDR_ADAPTER_TEST_SOURCES)
    get_property(adapter_libraries GLOBAL PROPERTY PDR_ADAPTER_TEST_LIBRARIES)

    if(adapter_sources AND userver_FOUND)
        add_executable(pdr_adapter_tests ${adapter_sources})
        target_link_libraries(pdr_adapter_tests PRIVATE ${adapter_libraries} userver::utest)
        add_test(NAME adapter COMMAND pdr_adapter_tests)

        # Схемы тел запросов читаются файлами, а не вкомпилированы: путь до
        # дерева нужен затем, чтобы прогон нашёл их из любого каталога сборки.
        target_compile_definitions(pdr_adapter_tests PRIVATE PDR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    endif()
endif()
