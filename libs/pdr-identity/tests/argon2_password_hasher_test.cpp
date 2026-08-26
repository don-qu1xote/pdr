#include "identity/infrastructure/auth/argon2_password_hasher.hpp"

#include <string>

#include <gtest/gtest.h>

#include "fakes/fake_secret_generator.hpp"

namespace pdr::identity {
namespace {

/// Дешевле рекомендованного считать нельзя — это отвергает домен, — поэтому
/// тест идёт на настоящей стоимости. Он от этого не быстрый, и это честная
/// цена: проверяется ровно то, что защита стоит работы.
PasswordRules Rules() {
    return PasswordRules::Compose(19456, 2, 1, 10).Value();
}

class Argon2Test : public ::testing::Test {
protected:
    pdr::testing::FakeSecretGenerator secrets_;
    Argon2PasswordHasher hasher_{secrets_};
};

TEST_F(Argon2Test, HashLooksLikeArgon2idAndVerifies) {
    const auto password = Password::Given("пароль-который-помнят").Value();

    const auto hash = hasher_.Hash(password, Rules());

    ASSERT_TRUE(hash.HasValue()) << hash.Failure().Detail();
    EXPECT_EQ(hash.Value().Value().rfind("$argon2id$", 0), 0U);
    EXPECT_TRUE(hasher_.Matches(password, hash.Value()));
}

TEST_F(Argon2Test, WrongPasswordDoesNotVerify) {
    const auto right = Password::Given("пароль-который-помнят").Value();
    const auto wrong = Password::Given("пароль-который-забыли").Value();

    const auto hash = hasher_.Hash(right, Rules());
    ASSERT_TRUE(hash.HasValue());

    EXPECT_FALSE(hasher_.Matches(wrong, hash.Value()));
}

/// СОЛЬ РАЗНАЯ У КАЖДОГО. Два одинаковых пароля дают разные записи — иначе по
/// утёкшей базе видно, у кого пароль совпадает с чьим, и подбирать надо один
/// раз на всех.
TEST_F(Argon2Test, TheSamePasswordHashesDifferentlyEveryTime) {
    const auto password = Password::Given("одинаковый-пароль").Value();

    const auto first = hasher_.Hash(password, Rules());
    const auto second = hasher_.Hash(password, Rules());

    ASSERT_TRUE(first.HasValue());
    ASSERT_TRUE(second.HasValue());
    EXPECT_NE(first.Value().Value(), second.Value().Value());
    EXPECT_TRUE(hasher_.Matches(password, first.Value()));
    EXPECT_TRUE(hasher_.Matches(password, second.Value()));
}

/// ПАРАМЕТРЫ СЧЁТА ЛЕЖАТ ВНУТРИ ЗАПИСИ. Стоимость подняли — старые хеши
/// продолжают проверяться своими параметрами; иначе смена стоимости означала бы,
/// что войти больше не может никто.
TEST_F(Argon2Test, OldHashesKeepWorkingAfterTheCostGoesUp) {
    const auto password = Password::Given("пароль-который-помнят").Value();
    const auto cheap = hasher_.Hash(password, Rules());
    ASSERT_TRUE(cheap.HasValue());

    const auto dearer = PasswordRules::Compose(65536, 3, 1, 10).Value();
    const auto fresh = hasher_.Hash(password, dearer);
    ASSERT_TRUE(fresh.HasValue());

    EXPECT_TRUE(hasher_.Matches(password, cheap.Value()))
        << "поднятие стоимости заперло вход всем, у кого пароль старый";
    EXPECT_TRUE(hasher_.Matches(password, fresh.Value()));
    EXPECT_NE(cheap.Value().Value(), fresh.Value().Value());
}

TEST_F(Argon2Test, CostIsWrittenIntoTheRecord) {
    const auto password = Password::Given("пароль-который-помнят").Value();

    const auto hash = hasher_.Hash(password, Rules());

    ASSERT_TRUE(hash.HasValue());
    EXPECT_NE(hash.Value().Value().find("m=19456"), std::string::npos);
    EXPECT_NE(hash.Value().Value().find("t=2"), std::string::npos);
    EXPECT_NE(hash.Value().Value().find("p=1"), std::string::npos);
}

}  // namespace
}  // namespace pdr::identity
