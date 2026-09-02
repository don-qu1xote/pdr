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

# Звенья конвейера запроса лежат здесь же, а не в сервисе: форма одна на всю
# систему, и разложенная по процессам она перестаёт быть одной.
add_library(pdr_http STATIC
    src/core/idempotency.cpp
    src/infrastructure/http/dynamic_config_key_lifetime.cpp
    src/infrastructure/http/error_mapping.cpp
    src/infrastructure/http/fingerprint.cpp
    src/infrastructure/http/idempotency.cpp
    src/infrastructure/http/middlewares/links.cpp
    src/infrastructure/http/middlewares/pipeline.cpp
    src/infrastructure/http/openapi_document.cpp
    src/infrastructure/http/problem.cpp
    src/infrastructure/http/request_body.cpp
    src/infrastructure/http/request_id.cpp
)
target_include_directories(pdr_http PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# pdr_api — ПОРОЖДЁННЫЕ ТИПЫ КОНТРАКТА, и связь публичная: тип тела виден в
# сигнатуре формы, значит виден и всякому, кто форму наследует.
target_link_libraries(pdr_http PUBLIC pdr_core pdr_application pdr_identity pdr_api userver::core)

# Адаптер ключей к Postgres — отдельной целью: сама форма собирается без
# драйвера базы, и проверяется она на фейке. Цель нужна тому, кто поднимает
# сервис, а не тому, кто проверяет форму.
add_library(pdr_http_postgres STATIC src/infrastructure/http/postgres_idempotency_keys.cpp)
target_include_directories(pdr_http_postgres PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pdr_http_postgres PUBLIC pdr_http pdr_postgres userver::postgresql)

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ CONTRACT/ADAPTER. Unit-прогону эти проверки не по силам и не по
    # праву: тело запроса, схема и сериализация — это userver, а его в
    # unit-цели нет намеренно (корневой CMakeLists, pdr_forbid_links).
    #
    # ТЕЛО ДВОЙНИКА ПОРОЖДАЕТСЯ ТЕМ ЖЕ ИНСТРУМЕНТОМ, что и настоящее.
    #
    # Форма обобщена по типу тела, и проверять её на типах входа значило бы
    # проверять заодно вход. Двойник же, собранный руками, проверял бы разбор,
    # которого в проде нет: настоящий разбор — порождённый.
    #
    # Контрактом эта схема не является и наружу не смотрит: за составом ручек
    # платформы следит scripts/check_openapi.py по docs/api/openapi.yaml.
    userver_target_generate_chaotic(pdr_http_double
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        OUTPUT_PREFIX pdr/http/double
        RELATIVE_TO "${CMAKE_CURRENT_SOURCE_DIR}/tests/schemas"
        LAYOUT "/components/schemas/([^/]*)/=pdr::infrastructure::http::testing::{0}"
        GENERATE_SERIALIZERS
        SCHEMAS "${CMAKE_CURRENT_SOURCE_DIR}/tests/schemas/booking.yaml"
    )

    pdr_adapter_test_sources(
        tests/authorized_handler_test.cpp
        tests/error_mapping_test.cpp
        tests/idempotency_test.cpp
        tests/problem_test.cpp
        tests/request_body_test.cpp
        tests/request_id_test.cpp
        tests/security_headers_test.cpp
    )
    pdr_adapter_test_libraries(pdr_http pdr_testing pdr_http_double)
endif()
