#include "infrastructure/crypto_secret_generator.hpp"

#include <set>
#include <string>

#include <gtest/gtest.h>
#include <pdr/testing/secret_generator_contract.hpp>

namespace pdr::testing {
namespace {

struct CryptoSecretGeneratorWorld final {
    const application::ports::SecretGenerator& Secrets() const noexcept {
        return secrets_;
    }

private:
    infrastructure::CryptoSecretGenerator secrets_;
};

}  // namespace

/// ТОТ ЖЕ contract-набор, что и на фейке. Разными фейк и настоящий обязаны
/// быть — предсказуемым и непредсказуемым; в остальном они обещают одно и то
/// же, и проверяется это одним набором.
PDR_SECRET_GENERATOR_CONTRACT(Crypto, CryptoSecretGeneratorWorld);

namespace {

/// Непредсказуемость набором тестов не проверяется — её обеспечивает тип.
/// А вот «источник вообще выдаёт разное» проверить можно, и молчащий генератор
/// (все нули, одно и то же значение) так виден сразу.
TEST(CryptoSecretGenerator, DoesNotStandStill) {
    const infrastructure::CryptoSecretGenerator secrets;

    std::set<std::string> seen;
    for (int issued = 0; issued < 1000; ++issued) {
        seen.insert(secrets.NextText(32));
    }

    EXPECT_EQ(seen.size(), 1000U);
}

TEST(CryptoSecretGenerator, IssuesUuidOfTheFourthVersion) {
    const infrastructure::CryptoSecretGenerator secrets;

    for (int issued = 0; issued < 100; ++issued) {
        const auto text = secrets.Next<core::PersonId>().ToString();

        EXPECT_EQ(text[14], '4') << "версия UUID не четвёртая: " << text;
        EXPECT_NE(std::string{"89ab"}.find(text[19]), std::string::npos)
            << "вариант UUID не тот: " << text;
    }
}

TEST(CryptoSecretGenerator, TextLengthFollowsTheAskedBytes) {
    const infrastructure::CryptoSecretGenerator secrets;

    EXPECT_EQ(secrets.NextText(16).size(), 22U);
    EXPECT_EQ(secrets.NextText(32).size(), 43U);
}

}  // namespace
}  // namespace pdr::testing
