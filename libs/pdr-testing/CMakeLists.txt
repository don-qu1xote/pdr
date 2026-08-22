# Общая оснастка тестов: фейки портов, билдеры данных и contract-наборы.
# Библиотека линкуется только в тесты и никогда — в сервис.
#
# Устройство пирамиды — docs/testing.md.
#
# Зависеть от контекстов ей нельзя (это платформенная библиотека, и проверяет
# запрет scripts/check_layers.py): поэтому здесь фейки платформенных портов и
# билдеры платформенных значений. Доменные билдеры живут в модуле своего
# контекста, рядом с его тестами.

add_library(pdr_testing STATIC
    src/fakes/fake_clock.cpp
    src/fakes/fake_id_generator.cpp
    src/fakes/fake_tenant_aware_repository.cpp
)
target_include_directories(pdr_testing
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_testing PUBLIC pdr_application)

if(PDR_BUILD_TESTS)
    # Contract-наборы — заголовки, поэтому цели нужны только тем, кто их
    # инстанцирует. gtest линкуется здесь, а не в pdr_testing: библиотека фейков
    # нужна и без него.
    target_link_libraries(pdr_testing PUBLIC GTest::gtest)

    # Билдеры — тоже код, и ошибка в них делает зелёными чужие тесты.
    pdr_unit_test_sources(tests/builders_test.cpp)
    pdr_unit_test_libraries(pdr_testing)

    # УРОВЕНЬ CONTRACT. Один и тот же набор, инстанцированный трижды: на фейке,
    # на настоящих адаптерах часов и идентификаторов и (в цели ниже, где есть
    # userver) на адаптере Postgres.
    add_executable(pdr_contract_tests
        tests/adapter_contract_test.cpp
        tests/repository_contract_test.cpp
    )
    target_link_libraries(pdr_contract_tests
                          PRIVATE pdr_testing pdr_infrastructure GTest::gtest_main)
    add_test(NAME contract COMMAND pdr_contract_tests)

    # ОТРИЦАТЕЛЬНЫЙ СЛУЧАЙ: набор с намеренно сломанным фейком ОБЯЗАН упасть.
    # Зелёный прогон здесь означает, что contract-набор ничего не проверяет, —
    # поэтому ctest ждёт от него провала.
    add_executable(pdr_broken_fake_contract_test tests/repository_contract_broken_test.cpp)
    target_link_libraries(pdr_broken_fake_contract_test PRIVATE pdr_testing GTest::gtest_main)
    add_test(NAME contract.broken_fake_must_fail COMMAND pdr_broken_fake_contract_test)
    set_tests_properties(contract.broken_fake_must_fail PROPERTIES WILL_FAIL TRUE)
endif()
