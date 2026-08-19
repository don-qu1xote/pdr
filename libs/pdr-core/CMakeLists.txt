# Три цели — по одной на слой. Граница слоёв здесь не комментарий, а то, что
# видит компоновщик: pdr_core не знает ни о userver, ни о Postgres, и добавить
# их туда нельзя — сборка остановится (см. pdr_forbid_external_links ниже).

add_library(pdr_core STATIC
    src/core/lesson_package.cpp
    src/core/money.cpp
    src/core/tariff.cpp
)
target_include_directories(pdr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(pdr_core PUBLIC cxx_std_20)
# Список зависимостей пуст, и это содержательный факт: домен собирается и
# тестируется без единой внешней библиотеки.

add_library(pdr_application STATIC
    src/application/quote_lesson_package.cpp
)
target_link_libraries(pdr_application PUBLIC pdr_core)

# userver появляется ровно на одном уровне — на самом внешнем. Пока его нет в
# окружении, два внутренних слоя собираются как обычно: это и есть проверка
# того, что домен от него не зависит.
find_package(userver QUIET COMPONENTS core postgresql)
if(userver_FOUND)
    add_library(pdr_infrastructure STATIC
        src/infrastructure/postgres_tariff_repository.cpp
        src/infrastructure/tariff_repository_component.cpp
    )
    target_link_libraries(pdr_infrastructure PUBLIC pdr_application userver::postgresql)
else()
    message(STATUS "pdr: userver не найден, цель pdr_infrastructure пропущена; "
                   "pdr_core и pdr_application собираются как обычно")
endif()

# «Ядро не линкуется с userver» — проверка, а не договорённость. Функция читает
# итоговые зависимости цели и роняет конфигурацию, если кто-то их дописал.
function(pdr_forbid_external_links target)
    set(forbidden userver pqxx PostgreSQL libpq)
    foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(links ${target} ${property})
        if(NOT links)
            continue()
        endif()
        foreach(link IN LISTS links)
            foreach(name IN LISTS forbidden)
                if(link MATCHES "(^|::|/|-)${name}")
                    message(FATAL_ERROR
                            "Слой core не может зависеть от ${link}: цель ${target}, "
                            "свойство ${property}. Адаптер этой библиотеки живёт в "
                            "infrastructure, а его интерфейс — портом в application.")
                endif()
            endforeach()
        endforeach()
    endforeach()
endfunction()

# Проверка откладывается на конец конфигурации: к этому моменту видны и те
# зависимости, которые дописали в другом каталоге.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pdr_forbid_external_links pdr_core)
