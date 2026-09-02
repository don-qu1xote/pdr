#include "infrastructure/http/request_body.hpp"

#include <string>

#include <pdr/http/double/booking.hpp>

#include <userver/utest/utest.hpp>

namespace pdr::infrastructure::http {
namespace {

using Body = testing::BookingRequest;

constexpr std::string_view kGood =
    R"({"student_id": "s-1", "starts_at": "2026-09-01T10:00:00Z", "minutes": 45})";

}  // namespace

UTEST(RequestBody, AGoodBodyBecomesAStruct) {
    std::string field;

    const auto body = ParseBody<Body>(kGood, field);

    ASSERT_TRUE(body.HasValue()) << body.Failure().Detail();
    EXPECT_TRUE(field.empty());
    EXPECT_EQ(body.Value().minutes, 45);
    EXPECT_EQ(body.Value().student_id, "s-1");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: отказ называет ПОЛЕ, а не «bad request».
UTEST(RequestBody, ARefusalNamesTheField) {
    std::string field;

    const auto body = ParseBody<Body>(
        R"({"student_id": "s-1", "starts_at": "2026-09-01T10:00:00Z", "minutes": "сорок пять"})",
        field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_NE(field.find("minutes"), std::string::npos)
        << "поле не названо: «" << field << "». Человек с разработчиком вдвоём гадают, какое";
    EXPECT_NE(body.Failure().Detail().find("minutes"), std::string::npos);
}

UTEST(RequestBody, AMissingFieldIsNamedToo) {
    std::string field;

    const auto body = ParseBody<Body>(R"({"student_id": "s-1", "minutes": 45})", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_EQ(field, "starts_at") << "поле не названо вовсе";
}

/// Проверка схемы, а не только разбор: длина и границы приходят из схемы вместе
/// с типом, и порождённый разборщик применяет их сам.
///
/// Путь до поля здесь приходит ТОЛЬКО ТЕКСТОМ сообщения — штатный разборщик на
/// этом роде отказа не даёт его спросить. Ради этого и написан `FieldOfMessage`.
UTEST(RequestBody, ARuleFromTheSchemaIsAppliedAndNamesTheField) {
    std::string field;

    const auto body = ParseBody<Body>(
        R"({"student_id": "", "starts_at": "2026-09-01T10:00:00Z", "minutes": 45})", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_EQ(field, "student_id");
}

UTEST(RequestBody, ABodyThatIsNotJsonSaysSo) {
    std::string field;

    const auto body = ParseBody<Body>("не json вовсе", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_not_json");
    EXPECT_TRUE(field.empty());
}

/// Лишнее поле — тоже отказ: тело, в котором клиент прислал `admin: true`,
/// разбирается молча только там, где схема этого не запрещает.
UTEST(RequestBody, AnExtraFieldIsRefusedAndNamed) {
    std::string field;

    const auto body = ParseBody<Body>(
        R"({"student_id": "s-1", "starts_at": "x", "minutes": 45, "admin": true})", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_EQ(field, "admin");
}

/// Сообщение без пути ни на какое поле не указывает, и выдумывать его нельзя.
UTEST(FieldOfMessage, SaysNothingWhenTheMessageNamesNoPath) {
    EXPECT_EQ(FieldOfMessage("JSON parse error at line 1 column 2"), "");
    EXPECT_EQ(FieldOfMessage(""), "");
}

}  // namespace pdr::infrastructure::http
