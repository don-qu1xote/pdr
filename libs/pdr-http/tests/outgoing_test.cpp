/// @file
/// ЧУЖОЙ HTTP: срок, бюджет повторов и квота — на настоящем клиенте и настоящем
/// сервере (PDR-ARCH-10).
///
/// Сервер здесь штатный (`utest::HttpServerMock`), клиент штатный
/// (`utest::CreateHttpClient`), бюджет и ведро токенов штатные. Своего в
/// проверке нет ничего, и проверяется в ней только НАША политика: сколько раз мы
/// уходим в сеть и при каких условиях не уходим вовсе.
#include "infrastructure/http/outgoing.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>

#include <userver/engine/sleep.hpp>
#include <userver/utest/http_client.hpp>
#include <userver/utest/http_server_mock.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/datetime.hpp>

#include "infrastructure/http/outgoing_component.hpp"

namespace pdr::infrastructure::http {
namespace {

using namespace std::chrono_literals;

userver::utils::RetryBudget Budget(bool enabled = true) {
    userver::utils::RetryBudgetSettings settings;
    settings.max_tokens = 100.0F;
    settings.token_ratio = 0.1F;
    settings.enabled = enabled;
    return userver::utils::RetryBudget{settings};
}

/// Исчерпать бюджет — столькими отказами, сколько в нём токенов.
void Exhaust(userver::utils::RetryBudget& budget) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        budget.AccountFail();
    }
}

userver::utils::TokenBucket Unbounded() {
    return userver::utils::TokenBucket::MakeUnbounded();
}

userver::utils::TokenBucket Empty() {
    return userver::utils::TokenBucket{
        0, userver::utils::TokenBucket::RefillPolicy{0, std::chrono::seconds{1}}};
}

}  // namespace

UTEST(Outgoing, AnExhaustedBudgetDoesNotGoOutASecondTime) {
    std::atomic<int> hits{0};
    userver::utest::HttpServerMock server{[&hits](const auto&) {
        ++hits;
        return userver::utest::HttpServerMock::HttpResponse{500, {}, "не сегодня"};
    }};

    auto client = userver::utest::CreateHttpClient();
    auto budget = Budget();
    Exhaust(budget);
    auto quota = Unbounded();

    const Outgoing outgoing{"probe", *client, 2s, 3, budget, quota};
    const auto answer = outgoing.Send(Repeatable::kYes, [&server](auto& http) {
        return http.CreateRequest().get(server.GetBaseUrl());
    });

    EXPECT_FALSE(answer.has_value()) << "лежащий сервис отдал ответ";
    EXPECT_EQ(hits.load(), 1) << "повтор ушёл в сеть при исчерпанном бюджете: "
                                 "именно так добивают лежащий сервис";
}

UTEST(Outgoing, AFreshBudgetAllowsTheRetry) {
    std::atomic<int> hits{0};
    userver::utest::HttpServerMock server{[&hits](const auto&) {
        ++hits;
        return userver::utest::HttpServerMock::HttpResponse{500, {}, "не сегодня"};
    }};

    auto client = userver::utest::CreateHttpClient();
    auto budget = Budget();
    auto quota = Unbounded();

    const Outgoing outgoing{"probe", *client, 2s, 3, budget, quota};
    const auto answer = outgoing.Send(Repeatable::kYes, [&server](auto& http) {
        return http.CreateRequest().get(server.GetBaseUrl());
    });

    EXPECT_FALSE(answer.has_value());
    EXPECT_GT(hits.load(), 1) << "повтор не ушёл, хотя бюджет цел: тогда бюджет ни на что "
                                 "не влияет";
}

/// Повтор неидемпотентного обращения — это второй платёж, а не вторая попытка.
UTEST(Outgoing, ANonRepeatableCallIsNeverRetried) {
    std::atomic<int> hits{0};
    userver::utest::HttpServerMock server{[&hits](const auto&) {
        ++hits;
        return userver::utest::HttpServerMock::HttpResponse{500, {}, "не сегодня"};
    }};

    auto client = userver::utest::CreateHttpClient();
    auto budget = Budget();
    auto quota = Unbounded();

    const Outgoing outgoing{"probe", *client, 2s, 3, budget, quota};
    const auto answer = outgoing.Send(Repeatable::kNo, [&server](auto& http) {
        return http.CreateRequest().post(server.GetBaseUrl(), std::string{"{}"});
    });

    EXPECT_FALSE(answer.has_value());
    EXPECT_EQ(hits.load(), 1) << "неидемпотентное обращение повторено (PDR-API-02)";
}

UTEST(Outgoing, AnExhaustedQuotaDoesNotGoOutAtAll) {
    std::atomic<int> hits{0};
    userver::utest::HttpServerMock server{[&hits](const auto&) {
        ++hits;
        return userver::utest::HttpServerMock::HttpResponse{200, {}, "ок"};
    }};

    auto client = userver::utest::CreateHttpClient();
    auto budget = Budget();
    auto quota = Empty();

    const Outgoing outgoing{"probe", *client, 2s, 3, budget, quota};
    const auto answer = outgoing.Send(Repeatable::kYes, [&server](auto& http) {
        return http.CreateRequest().get(server.GetBaseUrl());
    });

    EXPECT_FALSE(answer.has_value());
    EXPECT_EQ(hits.load(), 0) << "квота исчерпана, а мы всё равно пошли наружу";
}

/// Молчащий сервис не удлиняет ответ: срок направления обрывает ожидание.
UTEST(Outgoing, ASilentServiceCostsNoMoreThanTheTimeout) {
    userver::utest::HttpServerMock server{[](const auto&) {
        userver::engine::SleepFor(2s);
        return userver::utest::HttpServerMock::HttpResponse{200, {}, "поздно"};
    }};

    auto client = userver::utest::CreateHttpClient();
    auto budget = Budget();
    auto quota = Unbounded();

    const Outgoing outgoing{"probe", *client, 300ms, 1, budget, quota};

    const auto started = std::chrono::steady_clock::now();
    const auto answer = outgoing.Send(Repeatable::kNo, [&server](auto& http) {
        return http.CreateRequest().get(server.GetBaseUrl());
    });
    const auto took = std::chrono::steady_clock::now() - started;

    EXPECT_FALSE(answer.has_value());
    EXPECT_LT(took, 1s) << "молчащий сервис держал нас дольше собственного срока: "
                           "ровно так чужой сервис становится несущим";
}

TEST(OutgoingDeadlines, AnOutgoingTimeoutBeyondTheRequestDeadlineIsRefused) {
    const std::unordered_map<std::string, std::chrono::milliseconds> directions{
        {"payments", 3000ms},
        {"model", 6000ms},
    };

    const auto wrong = WhatIsWrongWithDeadlines(directions, 5000ms);
    ASSERT_TRUE(wrong.has_value()) << "срок наружу больше срока запроса объявлен годным";
    EXPECT_NE(wrong->find("model"), std::string::npos) << "отказ не называет направление";
}

TEST(OutgoingDeadlines, AnOutgoingTimeoutEqualToTheDeadlineIsRefusedToo) {
    const std::unordered_map<std::string, std::chrono::milliseconds> directions{{"video", 5000ms}};

    EXPECT_TRUE(WhatIsWrongWithDeadlines(directions, 5000ms).has_value())
        << "ровно совпавший срок оставляет запросу ноль времени на ответ";
}

TEST(OutgoingDeadlines, EveryTimeoutBelowTheDeadlineIsFine) {
    const std::unordered_map<std::string, std::chrono::milliseconds> directions{
        {"payments", 3000ms},
        {"video", 2000ms},
    };

    EXPECT_FALSE(WhatIsWrongWithDeadlines(directions, 5000ms).has_value());
}

}  // namespace pdr::infrastructure::http
