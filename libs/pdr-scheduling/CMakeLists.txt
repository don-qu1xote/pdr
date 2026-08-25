# Контекст scheduling: доступность, занятия, серии, отмены, окна бронирования.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Зависит от identity ровно одним заголовком — его публичным контрактом. Это
# видно и в CMake: pdr_identity отдаёт наружу только каталог contract.

add_library(pdr_scheduling STATIC
    src/scheduling/application/book_lesson.cpp
    src/scheduling/core/lesson.cpp
)
target_include_directories(pdr_scheduling PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_scheduling PUBLIC pdr_core pdr_events pdr_identity PRIVATE pdr_application)

# Публичного контракта у scheduling пока нет: его никто не спрашивает
# синхронно, о занятиях узнают событием. Контракт заводится вместе с первым
# вызывающим, а не заранее.

if(PDR_BUILD_TESTS)
    pdr_unit_test_sources(tests/book_lesson_test.cpp)
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_scheduling)

    # Граница контекста проверяется не только линтером, но и сборкой: цель ниже
    # ОБЯЗАНА не собираться, потому что внутренностей identity нет в путях
    # поиска этого модуля.
    add_executable(pdr_compile_fail_identity_internals EXCLUDE_FROM_ALL
                   tests/compile_fail/identity_internals.cpp)
    target_include_directories(pdr_compile_fail_identity_internals
                               PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_compile_fail_identity_internals PRIVATE pdr_scheduling)
    add_test(NAME unit.compile_fail.identity_internals
             COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                     --target pdr_compile_fail_identity_internals --config $<CONFIG>)
    set_tests_properties(unit.compile_fail.identity_internals PROPERTIES WILL_FAIL TRUE)
endif()
