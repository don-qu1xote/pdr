# Платформа: то, чем пользуются все контексты и что не принадлежит ни одному.
# Три цели — по одной на слой. Граница слоёв здесь не комментарий, а то, что
# видит компоновщик: pdr_core не знает ни о userver, ни о Postgres, и добавить
# их туда нельзя — сборка остановится (pdr_forbid_links в корневом CMakeLists).
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
# сервис без них — нет. Проверяются они contract-набором вместе с фейками
# (libs/pdr-testing/tests/adapter_contract_test.cpp).
add_library(pdr_infrastructure STATIC
    src/infrastructure/random_id_generator.cpp
    src/infrastructure/system_clock.cpp
)
target_include_directories(pdr_infrastructure PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_infrastructure PUBLIC pdr_core pdr_application)

# Адаптер к Postgres — отдельной целью, потому что он требует userver, а
# pdr_infrastructure линкуют contract-тесты. Без userver контекст остаётся
# собираемым и проверяемым целиком, кроме этого слоя; сам адаптер при этом не
# остаётся без присмотра — scripts/check_rls.py сверяет имя параметра сессии в нём
# с именем в политиках миграции.
# userver ищет корневой CMakeLists — один раз на весь проект.
if(userver_FOUND)
    add_library(pdr_postgres STATIC src/infrastructure/postgres_tenant_aware_repository.cpp)
    target_include_directories(pdr_postgres PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_postgres PUBLIC pdr_core pdr_application userver::postgresql)
endif()

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ UNIT: домен и порты на фейках. Свой исполняемый файл модуль не
    # заводит — все unit-тесты собираются в одну цель pdr_unit_tests.
    pdr_unit_test_sources(
        tests/errors_test.cpp
        tests/ids_test.cpp
        tests/tenant_scope_test.cpp
        tests/time_test.cpp
    )
    pdr_unit_test_libraries(pdr_core pdr_application)

    # Отрицательные тесты: эти цели ОБЯЗАНЫ не собираться. Без них «перепутать
    # нельзя» и «сессию не получить мимо области» — обещания, а не свойства.
    # Цели исключены из ALL, тест собирает их отдельно и ждёт провала.
    # Через двоеточие — с чем линковать: сессия хранилища живёт в оснастке
    # тестов, остальным случаям хватает ядра.
    foreach(case IN ITEMS ids_person_as_tenant:pdr_core
                          ids_implicit_to_string:pdr_core
                          ids_implicit_to_int:pdr_core
                          config_in_core:pdr_core
                          tenant_session_outside_scope:pdr_testing)
        string(REPLACE ":" ";" parts "${case}")
        list(GET parts 0 name)
        list(GET parts 1 needs)
        add_executable(pdr_compile_fail_${name} EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
        target_link_libraries(pdr_compile_fail_${name} PRIVATE ${needs})
        add_test(NAME unit.compile_fail.${name}
                 COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                         --target pdr_compile_fail_${name} --config $<CONFIG>)
        set_tests_properties(unit.compile_fail.${name} PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()

# «Платформа не линкуется с userver» — проверка, а не договорённость. Проверка
# отложена на конец конфигурации: к этому моменту видны и те зависимости, которые
# дописали в другом каталоге.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pdr_forbid_links pdr_core
               "Адаптер этой библиотеки живёт в infrastructure, а его интерфейс — портом в application."
               userver pqxx PostgreSQL libpq)
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pdr_forbid_links pdr_application
               "Адаптер этой библиотеки живёт в infrastructure, а его интерфейс — портом в application."
               userver pqxx PostgreSQL libpq)
