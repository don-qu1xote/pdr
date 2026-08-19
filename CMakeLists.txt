# Корень сборки ПДР. Каркас: здесь нет ни одной цели и не должно быть —
# библиотеки, сервисы и тесты заводит PDR-ARCH-01.
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
