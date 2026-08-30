#include "infrastructure/http/problem.hpp"

#include <string>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/utest/utest.hpp>

namespace pdr::infrastructure::http {
namespace {

Problem Some() {
    return Problem{ProblemType("slot_already_taken"),
                   "Состояние не позволяет",
                   409,
                   "это время у репетитора уже занято",
                   "/lessons",
                   "req-42",
                   std::nullopt};
}

}  // namespace

UTEST(Problem, RendersEveryAgreedMember) {
    const auto body = userver::formats::json::FromString(Render(Some()));

    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:slot_already_taken");
    EXPECT_EQ(body["title"].As<std::string>(), "Состояние не позволяет");
    EXPECT_EQ(body["status"].As<int>(), 409);
    EXPECT_EQ(body["detail"].As<std::string>(), "это время у репетитора уже занято");
    EXPECT_EQ(body["instance"].As<std::string>(), "/lessons");
    EXPECT_EQ(body["request_id"].As<std::string>(), "req-42");
}

/// Статус — число, а не строка: клиент сравнивает его с числом, и «409»
/// в кавычках ломает ровно те разборы, которые никто не проверяет.
UTEST(Problem, TheStatusIsANumber) {
    const auto body = userver::formats::json::FromString(Render(Some()));

    EXPECT_TRUE(body["status"].IsInt());
    EXPECT_FALSE(body["status"].IsString());
}

/// Подробность отказа приходит от домена и может содержать что угодно, включая
/// кавычки и переводы строк. Тело обязано остаться разбираемым.
UTEST(Problem, ADetailWithQuotesDoesNotBreakTheBody) {
    Problem problem = Some();
    problem.detail = "он сказал \"нет\"\nи ушёл";

    const auto body = userver::formats::json::FromString(Render(problem));

    EXPECT_EQ(body["detail"].As<std::string>(), "он сказал \"нет\"\nи ушёл");
}

UTEST(Problem, TheTypeIsAStableUrnBuiltFromTheCode) {
    EXPECT_EQ(ProblemType("session_expired"), "urn:pdr:error:session_expired");
    EXPECT_EQ(kProblemContentType, "application/problem+json");
}

}  // namespace pdr::infrastructure::http
