#include "core/secrets.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "application/verify_secrets.hpp"
#include "core/secret_string.hpp"
#include "fakes/fake_secret_source.hpp"

namespace pdr {
namespace {

using application::InspectSecrets;
using application::VerifySecrets;
using core::kEverySecret;
using core::SecretSpec;
using testing::FakeSecretSource;

/// Мир, в котором всё на месте: у каждого обязательного секрета своё значение.
///
/// Заполняется на месте, а не возвращается значением: источник секретов —
/// порт, у порта удалено копирование, и это правильно. Двух источников с одними
/// секретами не бывает.
void Complete(FakeSecretSource& source) {
    int number = 0;
    for (const auto& spec : kEverySecret) {
        source.Put(std::string{spec.name}, "значение-" + std::to_string(++number));
    }
}

std::vector<SecretSpec> RequiredNow() {
    std::vector<SecretSpec> needed;
    std::copy_if(kEverySecret.begin(),
                 kEverySecret.end(),
                 std::back_inserter(needed),
                 [](const SecretSpec& spec) { return spec.RequiredNow(); });
    return needed;
}

}  // namespace

TEST(Secrets, EverythingInPlaceStarts) {
    FakeSecretSource source;
    Complete(source);

    const auto checked = VerifySecrets(kEverySecret, source);

    ASSERT_TRUE(checked.HasValue()) << checked.Failure().Detail();
}

/// ГЛАВНЫЙ ТЕСТ ЗАДАЧИ: перебор по списку, а не один случай.
///
/// Один случай проверяет, что механизм в принципе работает. Перебор проверяет
/// то, ради чего он написан: секрет, заведённый завтра и забытый в проверке,
/// обязан уронить старт, а не пройти незамеченным.
TEST(Secrets, WithoutEachRequiredSecretTheServiceRefusesToStart) {
    const auto needed = RequiredNow();
    ASSERT_FALSE(needed.empty()) << "обязательных секретов нет: перебор ничего не доказывает";

    for (const auto& missing : needed) {
        FakeSecretSource source;
        Complete(source);
        source.Forget(missing.name);

        const auto checked = VerifySecrets(kEverySecret, source);

        ASSERT_FALSE(checked.HasValue()) << "сервис поднялся без секрета «" << missing.name << "»";
        EXPECT_EQ(checked.Failure().Code(), "secrets_incomplete");
        EXPECT_NE(checked.Failure().Detail().find(missing.name), std::string::npos)
            << "отказ не назвал переменную, которой нет: «" << checked.Failure().Detail() << "»";
    }
}

/// Пустой секрет — не значение, а забытая переменная. Перебор тот же.
TEST(Secrets, AnEmptyRequiredSecretRefusesToStartToo) {
    for (const auto& blank : RequiredNow()) {
        FakeSecretSource source;
        Complete(source);
        source.Put(std::string{blank.name}, "");

        const auto checked = VerifySecrets(kEverySecret, source);

        ASSERT_FALSE(checked.HasValue()) << "сервис поднялся с пустым «" << blank.name << "»";
        EXPECT_NE(checked.Failure().Detail().find(blank.name), std::string::npos);
    }
}

/// «Нет вовсе» и «есть и пусто» — разные ошибки у того, кто ставит сервис, и
/// сообщения им нужны разные.
TEST(Secrets, MissingAndBlankAreToldApart) {
    const auto needed = RequiredNow();
    const auto& first = needed.front();

    FakeSecretSource absent;
    Complete(absent);
    absent.Forget(first.name);

    FakeSecretSource blank;
    Complete(blank);
    blank.Put(std::string{first.name}, "");

    const auto about_absent = InspectSecrets(kEverySecret, absent);
    const auto about_blank = InspectSecrets(kEverySecret, blank);

    ASSERT_EQ(about_absent.size(), 1U);
    ASSERT_EQ(about_blank.size(), 1U);
    EXPECT_NE(about_absent.front().reason, about_blank.front().reason)
        << "забытую переменную и пустую объясняют одинаково";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: одинаковые значения у секретов разного назначения
/// роняют старт.
///
/// Проверяется на списке, где такие назначения есть: в дереве сегодня
/// обязателен один секрет, и «два разных назначения» на нём не выражаются.
TEST(Secrets, TheSameValueForDifferentPurposesRefusesToStart) {
    constexpr std::array<SecretSpec, 2> both{
        SecretSpec{"pays_with_it", core::SecretPurpose::kPaymentProvider, ""},
        SecretSpec{"checks_their_signature", core::SecretPurpose::kWebhookSigning, ""},
    };

    FakeSecretSource source;
    source.Put("pays_with_it", "одно-и-то-же").Put("checks_their_signature", "одно-и-то-же");

    const auto checked = VerifySecrets(both, source);

    ASSERT_FALSE(checked.HasValue())
        << "ключ, которым ходим мы, и ключ, которым подписывают нам, совпали — и сервис "
           "поднялся";
    EXPECT_NE(checked.Failure().Detail().find("checks_their_signature"), std::string::npos);
    EXPECT_NE(checked.Failure().Detail().find("назначения у них разные"), std::string::npos);
}

TEST(Secrets, DifferentValuesForDifferentPurposesStart) {
    constexpr std::array<SecretSpec, 2> both{
        SecretSpec{"pays_with_it", core::SecretPurpose::kPaymentProvider, ""},
        SecretSpec{"checks_their_signature", core::SecretPurpose::kWebhookSigning, ""},
    };

    FakeSecretSource source;
    source.Put("pays_with_it", "первое").Put("checks_their_signature", "второе");

    EXPECT_TRUE(VerifySecrets(both, source).HasValue());
}

/// Вставленное дважды из буфера обмена ловится и внутри одного назначения:
/// идентификатор магазина, равный секретному ключу, — не конфигурация, а опечатка.
TEST(Secrets, TheSameValueTwiceIsRefusedEvenWithinOnePurpose) {
    constexpr std::array<SecretSpec, 2> both{
        SecretSpec{"shop", core::SecretPurpose::kPaymentProvider, ""},
        SecretSpec{"key", core::SecretPurpose::kPaymentProvider, ""},
    };

    FakeSecretSource source;
    source.Put("shop", "вставили-дважды").Put("key", "вставили-дважды");

    const auto checked = VerifySecrets(both, source);

    ASSERT_FALSE(checked.HasValue());
    EXPECT_NE(checked.Failure().Detail().find("буфера обмена"), std::string::npos);
}

/// Секрет, которого некому читать, при старте не спрашивается: требовать ключ
/// от двери, которой нет, — верный способ приучить вписывать «change-me».
TEST(Secrets, AnAwaitedSecretIsNotAskedFor) {
    FakeSecretSource source;
    for (const auto& spec : kEverySecret) {
        if (spec.RequiredNow()) {
            source.Put(std::string{spec.name}, "значение-" + std::string{spec.name});
        }
    }

    EXPECT_TRUE(VerifySecrets(kEverySecret, source).HasValue())
        << "спросили секрет, читать который пока некому";
}

/// Все беды сразу, а не первая: шесть пропущенных переменных не должны стоить
/// шести запусков.
TEST(Secrets, AllComplaintsComeAtOnce) {
    constexpr std::array<SecretSpec, 3> three{
        SecretSpec{"first", core::SecretPurpose::kDatabase, ""},
        SecretSpec{"second", core::SecretPurpose::kPaymentProvider, ""},
        SecretSpec{"third", core::SecretPurpose::kVideoProvider, ""},
    };

    FakeSecretSource source;
    source.Put("second", "");

    const auto complaints = InspectSecrets(three, source);

    ASSERT_EQ(complaints.size(), 3U);
    EXPECT_EQ(complaints[0].name, "first");
    EXPECT_EQ(complaints[1].name, "second");
    EXPECT_EQ(complaints[2].name, "third");
}

TEST(Secrets, PurposesAreTheWordsTheRegistryKnows) {
    std::set<std::string_view> named;
    for (std::size_t index = 0; index < static_cast<std::size_t>(core::SecretPurpose::kBoundary);
         ++index) {
        const auto purpose = static_cast<core::SecretPurpose>(index);
        const auto parsed = core::ParseSecretPurpose(Name(purpose));

        ASSERT_TRUE(parsed.has_value()) << Name(purpose);
        EXPECT_EQ(*parsed, purpose);
        named.insert(Name(purpose));
    }

    EXPECT_EQ(named.size(), static_cast<std::size_t>(core::SecretPurpose::kBoundary))
        << "два назначения названы одинаково";
    EXPECT_FALSE(core::ParseSecretPurpose("прочее").has_value());
}

TEST(Secrets, NoTwoRegistryEntriesShareAName) {
    std::set<std::string_view> named;
    for (const auto& spec : kEverySecret) {
        EXPECT_TRUE(named.insert(spec.name).second) << spec.name << ": имя названо дважды";
    }
}

/// Секрет не выводится. Компилятор стережёт это отдельно
/// (`unit.compile_fail.secret_string_to_log`); здесь — что достать значение
/// можно, и ровно одним заметным способом.
TEST(SecretString, RevealsOnlyWhenAskedByName) {
    const core::SecretString secret{"настоящее-значение"};

    EXPECT_EQ(secret.Reveal(), "настоящее-значение");
    EXPECT_FALSE(secret.Empty());
    EXPECT_TRUE(core::SecretString{}.Empty());
    EXPECT_EQ(secret, core::SecretString{"настоящее-значение"});
    EXPECT_NE(secret, core::SecretString{"другое"});
}

TEST(SecretString, HasAMaskToSayItWithoutShowingIt) {
    std::ostringstream dump;
    dump << "postgres_dsn=" << core::kSecretMask;

    EXPECT_EQ(dump.str(), "postgres_dsn=«скрыто»");
}

}  // namespace pdr
