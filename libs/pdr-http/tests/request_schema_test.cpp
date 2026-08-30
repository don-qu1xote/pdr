#include "infrastructure/http/request_schema.hpp"

#include <string>

#include <userver/utest/utest.hpp>

namespace pdr::infrastructure::http {
namespace {

const std::string kSchemaFile =
    std::string{PDR_SOURCE_DIR} + "/libs/pdr-http/tests/schemas/book_lesson.json";

RequestSchema Loaded() {
    const auto schema = RequestSchema::FromFile(kSchemaFile);
    EXPECT_TRUE(schema.HasValue());
    return schema.Value();
}

}  // namespace

UTEST(RequestSchema, ReadsTheFileNextToTheHandler) {
    const auto schema = RequestSchema::FromFile(kSchemaFile);

    ASSERT_TRUE(schema.HasValue()) << schema.Failure().Code();
}

/// Схема ломается при сборке хендлера, а не у первого человека, который на неё
/// наткнулся.
UTEST(RequestSchema, AMissingFileIsRefusedAtOnce) {
    const auto schema = RequestSchema::FromFile("/no/such/schema.json");

    ASSERT_FALSE(schema.HasValue());
    EXPECT_EQ(schema.Failure().Code(), "schema_missing");
}

UTEST(RequestSchema, TextThatIsNotASchemaIsRefusedAtOnce) {
    const auto schema = RequestSchema::FromText("{ это не json");

    ASSERT_FALSE(schema.HasValue());
    EXPECT_EQ(schema.Failure().Code(), "schema_malformed");
}

UTEST(RequestSchema, AGoodBodyPasses) {
    const auto schema = Loaded();
    std::string field;

    const auto body = schema.Parse(
        R"({"student_id": "s-1", "starts_at": "2026-09-01T10:00:00Z", "minutes": 45})", field);

    ASSERT_TRUE(body.HasValue()) << body.Failure().Detail();
    EXPECT_TRUE(field.empty());
    EXPECT_EQ(body.Value()["minutes"].As<int>(), 45);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: отказ называет ПОЛЕ, а не «bad request».
UTEST(RequestSchema, ARefusalNamesTheField) {
    const auto schema = Loaded();
    std::string field;

    const auto body = schema.Parse(
        R"({"student_id": "s-1", "starts_at": "2026-09-01T10:00:00Z", "minutes": "сорок пять"})",
        field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_NE(field.find("minutes"), std::string::npos)
        << "поле не названо: «" << field << "». Человек с разработчиком вдвоём гадают, какое";
    EXPECT_NE(body.Failure().Detail().find("minutes"), std::string::npos);
}

UTEST(RequestSchema, AMissingFieldIsNamedToo) {
    const auto schema = Loaded();
    std::string field;

    const auto body = schema.Parse(R"({"student_id": "s-1", "minutes": 45})", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
    EXPECT_FALSE(field.empty()) << "поле не названо вовсе";
}

UTEST(RequestSchema, ABodyThatIsNotJsonSaysSo) {
    const auto schema = Loaded();
    std::string field;

    const auto body = schema.Parse("не json вовсе", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_not_json");
    EXPECT_TRUE(field.empty());
}

/// Лишнее поле — тоже отказ: тело, в котором клиент прислал `admin: true`,
/// разбирается молча только там, где схема этого не запрещает.
UTEST(RequestSchema, AnExtraFieldIsRefused) {
    const auto schema = Loaded();
    std::string field;

    const auto body = schema.Parse(
        R"({"student_id": "s-1", "starts_at": "x", "minutes": 45, "admin": true})", field);

    ASSERT_FALSE(body.HasValue());
    EXPECT_EQ(body.Failure().Code(), "request_field_invalid");
}

}  // namespace pdr::infrastructure::http
