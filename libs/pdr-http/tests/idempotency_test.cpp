#include "core/idempotency.hpp"

#include <set>
#include <string>

#include <userver/utest/utest.hpp>

#include "infrastructure/http/fingerprint.hpp"
#include "infrastructure/http/idempotency.hpp"

namespace pdr::http {
namespace {

constexpr std::array kEveryMethod{Method::kGet,
                                  Method::kHead,
                                  Method::kOptions,
                                  Method::kPost,
                                  Method::kPut,
                                  Method::kPatch,
                                  Method::kDelete};

static_assert(kEveryMethod.size() == static_cast<std::size_t>(Method::kBoundary),
              "метод заведён, а в перечне его нет: «меняет ли состояние» перестанет "
              "проверяться перебором");

}  // namespace

/// Ключ спрашивается со ВСЕХ четырёх меняющих методов, включая DELETE.
UTEST(Idempotency, EveryChangingMethodNeedsAKeyAndNoOtherDoes) {
    std::set<std::string_view> changing;
    for (const auto method : kEveryMethod) {
        if (Mutating(method)) {
            changing.insert(Name(method));
        }
    }

    EXPECT_EQ(changing, (std::set<std::string_view>{"POST", "PUT", "PATCH", "DELETE"}))
        << "список меняющих методов разошёлся: DELETE повторяется по оборванной связи ровно "
           "так же, как POST, и второе удаление попадает уже в чужое";
}

UTEST(Idempotency, AnUnknownMethodChangesNothingByDefault) {
    EXPECT_FALSE(Mutating(Method::kBoundary));
}

UTEST(Idempotency, AKeyTooShortToBeUniqueIsRefused) {
    EXPECT_FALSE(IdempotencyKey::Parse("1").HasValue());
    EXPECT_FALSE(IdempotencyKey::Parse("").HasValue());
    EXPECT_FALSE(IdempotencyKey::Parse(std::string(IdempotencyKey::kShortest - 1, 'a')).HasValue());
    EXPECT_TRUE(IdempotencyKey::Parse(std::string(IdempotencyKey::kShortest, 'a')).HasValue());
}

UTEST(Idempotency, AKeyTooLongIsRefused) {
    EXPECT_TRUE(IdempotencyKey::Parse(std::string(IdempotencyKey::kLongest, 'a')).HasValue());
    EXPECT_FALSE(IdempotencyKey::Parse(std::string(IdempotencyKey::kLongest + 1, 'a')).HasValue());
}

/// Ключ приходит от клиента и уходит в журнал — как и след запроса.
UTEST(Idempotency, AKeyWithWhitespaceOrControlCharactersIsRefused) {
    for (const auto* brought : {"с пробелом", "пере\nвод", "воз\rврат", "\x01управляющий"}) {
        EXPECT_FALSE(IdempotencyKey::Parse(brought).HasValue()) << brought;
    }
}

UTEST(Idempotency, AFingerprintIsSixtyFourLowercaseHexDigits) {
    EXPECT_TRUE(RequestFingerprint::Parse(std::string(64, 'a')).HasValue());
    EXPECT_FALSE(RequestFingerprint::Parse(std::string(63, 'a')).HasValue());
    EXPECT_FALSE(RequestFingerprint::Parse(std::string(64, 'A')).HasValue())
        << "верхний регистр принят: два написания одного отпечатка — это два разных ключа";
    EXPECT_FALSE(RequestFingerprint::Parse(std::string(64, 'z')).HasValue());
}

/// «То же самое тело» означает буквально то же самое. Клиент, переставивший
/// поля местами, прислал другой запрос — и нормализации здесь нет намеренно:
/// это правило, о котором клиент не знает.
UTEST(Idempotency, TheFingerprintFollowsTheBytesAndNothingElse) {
    const auto one = infrastructure::http::FingerprintOf(R"({"a": 1, "b": 2})");
    const auto same = infrastructure::http::FingerprintOf(R"({"a": 1, "b": 2})");
    const auto reordered = infrastructure::http::FingerprintOf(R"({"b": 2, "a": 1})");

    EXPECT_EQ(one, same);
    EXPECT_NE(one, reordered);
    EXPECT_EQ(one.Value().size(), 64U);
}

UTEST(Idempotency, StatesAreTheWordsTheDatabaseKnows) {
    for (const auto state : {KeyState::kInProgress, KeyState::kCompleted}) {
        const auto parsed = ParseKeyState(Name(state));

        ASSERT_TRUE(parsed.has_value()) << Name(state);
        EXPECT_EQ(*parsed, state);
    }
    EXPECT_FALSE(ParseKeyState("failed").has_value())
        << "состояния failed нет: отказавшая операция откатывается вместе со строкой ключа";
}

UTEST(Idempotency, TheLifetimeStaysInsideItsRegisteredLimits) {
    EXPECT_FALSE(KeyLifetime::Compose(0).HasValue());
    EXPECT_FALSE(KeyLifetime::Compose(KeyLifetime::kLongestHours + 1).HasValue());

    const auto day = KeyLifetime::Compose(24);
    ASSERT_TRUE(day.HasValue()) << day.Failure().Code();

    const auto now = core::Instant::FromUnixMicros(1'700'000'000'000'000);
    EXPECT_EQ(day.Value().ExpiresFrom(now) - now, std::chrono::hours{24});
}

/// Все три исхода занятия ключа названы: перебор по границе списка.
UTEST(Idempotency, EveryClaimOutcomeHasAName) {
    std::set<std::string_view> named;
    for (int index = 0; index < static_cast<int>(ClaimOutcome::kBoundary); ++index) {
        named.insert(Name(static_cast<ClaimOutcome>(index)));
    }

    EXPECT_EQ(named.size(), static_cast<std::size_t>(ClaimOutcome::kBoundary));
}

/// Перевод метода транспорта на наш язык: незнакомое до правил не доходит.
UTEST(Idempotency, TheTransportMethodIsTranslatedAndTheUnknownIsNotGuessed) {
    using userver::server::http::HttpMethod;

    EXPECT_EQ(infrastructure::http::Translate(HttpMethod::kPost), Method::kPost);
    EXPECT_EQ(infrastructure::http::Translate(HttpMethod::kDelete), Method::kDelete);
    EXPECT_EQ(infrastructure::http::Translate(HttpMethod::kGet), Method::kGet);
    EXPECT_EQ(infrastructure::http::Translate(HttpMethod::kUnknown), Method::kBoundary);
    EXPECT_FALSE(Mutating(infrastructure::http::Translate(HttpMethod::kUnknown)));
}

}  // namespace pdr::http
