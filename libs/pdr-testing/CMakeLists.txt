# Общая оснастка тестов: фейки портов и минимальная проверка условий.
# Библиотека линкуется только в тесты и никогда — в сервис.

add_library(pdr_testing STATIC
    src/testing/check.cpp
    src/testing/fake_clock.cpp
    src/testing/fake_id_generator.cpp
)
target_include_directories(pdr_testing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_testing PUBLIC pdr_application)
