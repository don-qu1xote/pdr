#include "identity/infrastructure/auth/dynamic_config_auth_settings.hpp"

#include <chrono>
#include <string>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/utest/utest.hpp>

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;

userver::formats::json::Value Passwords(int memory_kib, int iterations, int parallelism) {
    return userver::formats::json::FromString(R"({"memory_kib": )" + std::to_string(memory_kib) +
                                              R"(, "iterations": )" + std::to_string(iterations) +
                                              R"(, "parallelism": )" + std::to_string(parallelism) +
                                              R"(, "min_length": 10})");
}

userver::formats::json::Value Throttle(int window_minutes, int per_account, int per_address) {
    return userver::formats::json::FromString(
        R"({"window_minutes": )" + std::to_string(window_minutes) + R"(, "per_account": )" +
        std::to_string(per_account) + R"(, "per_address": )" + std::to_string(per_address) + "}");
}

userver::formats::json::Value Lifetimes(int session_hours,
                                        int invitation_hours,
                                        int reset_minutes) {
    return userver::formats::json::FromString(
        R"({"session_hours": )" + std::to_string(session_hours) + R"(, "invitation_hours": )" +
        std::to_string(invitation_hours) + R"(, "password_reset_minutes": )" +
        std::to_string(reset_minutes) + "}");
}

}  // namespace

/// Источник конфигов недоступен — сервис поднимается на умолчаниях из кода, и
/// они годные. Умолчание, которое схема тут же объявляет негодным, — ловушка,
/// не видная ни в одном тесте.
UTEST(DynamicConfigAuthSettings, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigAuthSettings settings{storage.GetSource()};

    const auto passwords = settings.Passwords();
    ASSERT_TRUE(passwords.HasValue()) << passwords.Failure().Code();
    EXPECT_EQ(passwords.Value().MemoryKib(), 19456U);

    const auto throttle = settings.Throttle();
    ASSERT_TRUE(throttle.HasValue()) << throttle.Failure().Code();
    EXPECT_TRUE(throttle.Value().Window() == 15min);

    const auto lifetimes = settings.Lifetimes();
    ASSERT_TRUE(lifetimes.HasValue()) << lifetimes.Failure().Code();
    EXPECT_TRUE(lifetimes.Value().PasswordReset() == 30min);
}

UTEST(DynamicConfigAuthSettings, AppliesChangeWithoutBeingRecreated) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{kPasswordRules, Passwords(19456, 2, 1)}});
    const DynamicConfigAuthSettings settings{storage.GetSource()};

    EXPECT_EQ(settings.Passwords().Value().MemoryKib(), 19456U);

    storage.Extend({{kPasswordRules, Passwords(65536, 3, 2)}});

    const auto after = settings.Passwords();
    ASSERT_TRUE(after.HasValue());
    EXPECT_EQ(after.Value().MemoryKib(), 65536U);
    EXPECT_EQ(after.Value().Iterations(), 3U);
}

/// СВЯЗЬ МЕЖДУ ВЕЛИЧИНАМИ СХЕМА НЕ ВЫРАЖАЕТ, и её ловит домен: стоимость счёта,
/// при которой памяти меньше, чем нужно нитям, отвергается целиком.
UTEST(DynamicConfigAuthSettings, RefusesCostTheDomainCallsImpossible) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{kPasswordRules, Passwords(1024, 2, 1)}});
    const DynamicConfigAuthSettings settings{storage.GetSource()};

    const auto refused = settings.Passwords();

    ASSERT_FALSE(refused.HasValue()) << "дешёвый счёт хеша принят";
    EXPECT_EQ(refused.Failure().Code(), "password_memory_too_small");
}

/// Порог по адресу строже порога по записи запирает целый класс из школы, у
/// которого один адрес на всех.
UTEST(DynamicConfigAuthSettings, RefusesThresholdsThatLockAWholeClass) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{kLoginThrottle, Throttle(15, 10, 5)}});
    const DynamicConfigAuthSettings settings{storage.GetSource()};

    const auto refused = settings.Throttle();

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "throttle_address_below_account");
}

/// Ссылка сброса, живущая дольше приглашения, — самая опасная строка в системе.
UTEST(DynamicConfigAuthSettings, RefusesResetLinkThatOutlivesAnInvitation) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{kAuthLifetimes, Lifetimes(720, 1, 120)}});
    const DynamicConfigAuthSettings settings{storage.GetSource()};

    const auto refused = settings.Lifetimes();

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "auth_reset_outlives_invitation");
}

}  // namespace pdr::identity
