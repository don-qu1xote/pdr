# Платформа: то, чем пользуются все контексты и что не принадлежит ни одному.
# Три цели — по одной на слой. Граница слоёв здесь не комментарий, а то, что
# видит компоновщик: pdr_core не знает ни о userver, ни о Postgres, и добавить
# их туда нельзя — сборка остановится (см. pdr_forbid_external_links ниже).
#
# Доменных правил здесь нет: они живут в модулях контекстов (libs/pdr-<контекст>),
# см. docs/architecture/context-map.md.

add_library(pdr_core STATIC
    src/core/errors.cpp
    src/core/money.cpp
    src/core/types/ids.cpp
    src/core/types/time.cpp
)
target_include_directories(pdr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(pdr_core PUBLIC cxx_std_20)
# Список зависимостей пуст, и это содержательный факт: домен собирается и
# тестируется без единой внешней библиотеки.

# Порты платформы — часы и генератор идентификаторов — только заголовки:
# у слоя application здесь нет ни строчки реализации, и цель это показывает.
add_library(pdr_application INTERFACE)
target_link_libraries(pdr_application INTERFACE pdr_core)

# Настоящие часы и настоящий генератор идентификаторов. userver им не нужен,
# поэтому цель собирается везде: подменить часы в тесте можно, а вот собрать
# сервис без них — нет.
add_library(pdr_infrastructure STATIC
    src/infrastructure/random_id_generator.cpp
    src/infrastructure/system_clock.cpp
)
target_include_directories(pdr_infrastructure PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_infrastructure PUBLIC pdr_core pdr_application)

if(PDR_BUILD_TESTS)
    # Тест — обычная программа: код возврата ноль, если все проверки прошли.
    # Никакого фреймворка, никакой базы, никакого докера; полноценный gtest
    # заводится отдельной задачей области TST.
    foreach(suite IN ITEMS errors ids infrastructure time)
        add_executable(pdr_core_${suite}_test tests/${suite}_test.cpp)
        target_link_libraries(pdr_core_${suite}_test PRIVATE pdr_infrastructure pdr_testing)
        add_test(NAME core.${suite} COMMAND pdr_core_${suite}_test)
    endforeach()

    # Отрицательный тест типизации: эти цели ОБЯЗАНЫ не собираться. Без него
    # StrongId не проверен — «перепутать нельзя» превращается в обещание.
    # Цели исключены из ALL, тест собирает их отдельно и ждёт провала.
    foreach(case IN ITEMS ids_person_as_tenant ids_implicit_to_string ids_implicit_to_int)
        add_executable(pdr_compile_fail_${case} EXCLUDE_FROM_ALL tests/compile_fail/${case}.cpp)
        target_link_libraries(pdr_compile_fail_${case} PRIVATE pdr_core)
        add_test(NAME core.compile_fail.${case}
                 COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                         --target pdr_compile_fail_${case} --config $<CONFIG>)
        set_tests_properties(core.compile_fail.${case} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()

# «Платформа не линкуется с userver» — проверка, а не договорённость. Функция
# читает итоговые зависимости цели и роняет конфигурацию, если кто-то их дописал.
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
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pdr_forbid_external_links pdr_application)
