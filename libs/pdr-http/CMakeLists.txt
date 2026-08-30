# Форма HTTP-слоя: одна на всю систему.
#
# Не контекст: предметных правил здесь нет ни одного. Здесь задан порядок шагов
# запроса, единый формат отказа (RFC 9457), отображение доменных ошибок в
# статусы и заголовки безопасности — то, что иначе каждая ручка решала бы
# заново и каждая по-своему.
#
# Зависит от identity ровно одним заголовком — его публичным контрактом: у
# хендлера один вопрос к политике, и содержимого ответа он не знает.
#
# Собирается только там, где есть userver: разбор запроса, JSON и схемы берутся
# у него, своих в дереве нет и не будет (ADR-0013).
if(NOT userver_FOUND)
    return()
endif()

add_library(pdr_http STATIC
    src/infrastructure/http/error_mapping.cpp
    src/infrastructure/http/problem.cpp
    src/infrastructure/http/request_id.cpp
    src/infrastructure/http/request_schema.cpp
)
target_include_directories(pdr_http PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_http PUBLIC pdr_core pdr_application pdr_identity userver::core)

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ CONTRACT/ADAPTER. Unit-прогону эти проверки не по силам и не по
    # праву: тело запроса, схема и сериализация — это userver, а его в
    # unit-цели нет намеренно (корневой CMakeLists, pdr_forbid_links).
    #
    # Схемы для проверки лежат файлами рядом: схема, вкомпилированная в тест,
    # проверяла бы разбор, но не чтение файла, — а падать оно будет именно на
    # чтении.
    pdr_adapter_test_sources(
        tests/authorized_handler_test.cpp
        tests/error_mapping_test.cpp
        tests/problem_test.cpp
        tests/request_id_test.cpp
        tests/request_schema_test.cpp
        tests/security_headers_test.cpp
    )
    pdr_adapter_test_libraries(pdr_http pdr_testing)
endif()
