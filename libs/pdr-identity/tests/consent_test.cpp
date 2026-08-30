#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "identity/application/give_consent.hpp"
#include "identity/application/show_my_data.hpp"
#include "identity/core/personal_data.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

const auto kTenant = Numbered<core::TenantId>(1);
const auto kChild = Numbered<core::PersonId>(10);
const auto kGuardian = Numbered<core::PersonId>(11);
const auto kTeenager = Numbered<core::PersonId>(20);
const auto kAdult = Numbered<core::PersonId>(30);
const auto kStranger = Numbered<core::PersonId>(40);

const BirthDate kChildBorn = BirthDate::Of(2018, 3, 1).Value();
const BirthDate kTeenagerBorn = BirthDate::Of(2010, 3, 1).Value();
const BirthDate kAdultBorn = BirthDate::Of(1990, 3, 1).Value();

/// Согласия в памяти. Отозванные остаются: на вопрос «а было ли согласие в
/// марте» отвечает строка, а не её отсутствие.
class FakeConsentRecords final : public ports::Consents {
public:
    std::vector<ConsentRecord> Of(const core::TenantId& tenant,
                                  const core::PersonId& subject) const override {
        std::vector<ConsentRecord> found;
        for (const auto& row : rows_) {
            if (row.Tenant() == tenant && row.Subject() == subject) {
                found.push_back(row);
            }
        }
        return found;
    }

    void Save(const ConsentRecord& record) override {
        const auto same =
            std::find_if(rows_.begin(), rows_.end(), [&record](const ConsentRecord& row) {
                return row.Id() == record.Id();
            });
        if (same == rows_.end()) {
            rows_.push_back(record);
        } else {
            *same = record;
        }
    }

    std::size_t Size() const noexcept {
        return rows_.size();
    }

private:
    std::vector<ConsentRecord> rows_;
};

/// Перечень версий: что действует сейчас и было ли существенное изменение.
///
/// Существенность назначается здесь, а не выводится: ровно так же, как её
/// назначает человек при выпуске версии.
class FakeVersions final : public ports::PolicyVersions {
public:
    void Release(int number, VersionChange change) {
        current_ = PolicyVersion::Of(number).Value();
        changes_.insert_or_assign(number, change);
    }

    PolicyVersion Current() const override {
        return current_;
    }

    bool SubstantialAfter(const PolicyVersion& accepted) const override {
        for (const auto& [number, change] : changes_) {
            if (number > accepted.Number() && change == VersionChange::kSubstantial) {
                return true;
            }
        }
        return false;
    }

private:
    PolicyVersion current_{PolicyVersion::Of(1).Value()};
    std::map<int, VersionChange> changes_{{1, VersionChange::kSubstantial}};
};

class ConsentTest : public ::testing::Test {
protected:
    ConsentTest() {
        world_.birth_dates.Put(kTenant, kChild, kChildBorn);
        world_.birth_dates.Put(kTenant, kTeenager, kTeenagerBorn);
        world_.birth_dates.Put(kTenant, kAdult, kAdultBorn);
        world_.guardianships.Establish(kTenant, kGuardian, kChild);
        world_.guardianships.Establish(kTenant, kGuardian, kTeenager);
        world_.clock.SetNow(AgeStatus::TurnsAt(kAdultBorn, 34));
    }

    GiveConsent Giving() {
        return GiveConsent{records_,
                           versions_,
                           world_.guardianships,
                           world_.birth_dates,
                           world_.maturity,
                           world_.ids,
                           world_.clock};
    }

    ShowMyData Showing() const {
        return ShowMyData{records_, versions_, world_.clock};
    }

    core::Result<ConsentRecord> Give(const core::PersonId& subject,
                                     const core::PersonId& by,
                                     ConsentKind kind = ConsentKind::kProcessing) {
        return Giving().Execute(
            GiveConsentRequest{kTenant, subject, by, kind, ConsentAction::kSignUpCheckbox});
    }

    testing::AccessWorld world_;
    FakeConsentRecords records_;
    FakeVersions versions_;
};

}  // namespace

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: согласие хранится с версией и действием.
///
/// Взрослый при этом соглашается САМ, и опеки над ним не заводится ни на
/// секунду: у него опекуна нет вовсе (ADR-0020).
TEST_F(ConsentTest, AConsentIsStoredWithItsVersionAndTheActionThatGaveIt) {
    ASSERT_TRUE(world_.guardianships.GuardiansOf(kTenant, kAdult).empty())
        << "мир проверки завёл опеку над взрослым: она бы и объясняла успех";

    const auto given = Give(kAdult, kAdult);

    ASSERT_TRUE(given.HasValue()) << given.Failure().Code();
    EXPECT_EQ(given.Value().Version(), versions_.Current());
    EXPECT_EQ(given.Value().Action(), ConsentAction::kSignUpCheckbox);
    EXPECT_EQ(given.Value().GivenAt(), world_.clock.Now());
    EXPECT_EQ(given.Value().Subject(), kAdult);
    EXPECT_EQ(given.Value().GivenBy(), kAdult);
    EXPECT_FALSE(given.Value().ByGuardian());
    EXPECT_EQ(records_.Size(), 1U) << "согласие подразумевается, а не хранится";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: за ребёнка соглашается опекун, и без этого ученик
/// не заводится.
TEST_F(ConsentTest, AChildDoesNotConsentForHimself) {
    const auto himself = Give(kChild, kChild);

    ASSERT_FALSE(himself.HasValue()) << "ребёнок согласился за себя сам";
    EXPECT_EQ(himself.Failure().Code(), "consent_needs_guardian");
    EXPECT_EQ(records_.Size(), 0U);
}

TEST_F(ConsentTest, TheGuardianConsentsForTheChild) {
    const auto by_guardian = Give(kChild, kGuardian);

    ASSERT_TRUE(by_guardian.HasValue()) << by_guardian.Failure().Code();
    EXPECT_EQ(by_guardian.Value().Subject(), kChild);
    EXPECT_EQ(by_guardian.Value().GivenBy(), kGuardian);
    EXPECT_TRUE(by_guardian.Value().ByGuardian());
}

/// Посторонний не опекун, и согласия за чужого ребёнка не даёт.
TEST_F(ConsentTest, AStrangerCannotConsentForSomeoneElsesChild) {
    const auto refused = Give(kChild, kStranger);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "consent_not_yours_to_give");
}

/// С первого порога ученик соглашается сам в той части, где действует сам.
TEST_F(ConsentTest, FromTheFirstThresholdTheStudentConsentsHimself) {
    world_.clock.SetNow(AgeStatus::TurnsAt(kTeenagerBorn, 14));

    const auto himself = Give(kTeenager, kTeenager);

    ASSERT_TRUE(himself.HasValue()) << himself.Failure().Code();
    EXPECT_FALSE(himself.Value().ByGuardian());
}

TEST_F(ConsentTest, ADayBeforeTheThresholdHeStillCannot) {
    world_.clock.SetNow(AgeStatus::TurnsAt(kTeenagerBorn, 14) - std::chrono::hours{24});

    EXPECT_FALSE(Give(kTeenager, kTeenager).HasValue());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: две галочки, а не одна. Согласие на запись —
/// отдельное, и без него продукт работает.
TEST_F(ConsentTest, ProcessingAndRecordingsAreTwoSeparateConsents) {
    ASSERT_TRUE(Give(kAdult, kAdult, ConsentKind::kProcessing).HasValue());

    const auto shown = Showing().Execute(kTenant, kAdult);
    ASSERT_TRUE(shown.HasValue());

    ASSERT_EQ(shown.Value().consents.size(), 1U);
    EXPECT_EQ(shown.Value().consents.front().kind, ConsentKind::kProcessing)
        << "одна галочка закрыла оба вида: у них разный правовой вес";

    ASSERT_TRUE(Give(kAdult, kAdult, ConsentKind::kRecordings).HasValue());
    const auto both = Showing().Execute(kTenant, kAdult);
    ASSERT_TRUE(both.HasValue());
    EXPECT_EQ(both.Value().consents.size(), 2U);
}

TEST_F(ConsentTest, EveryKindOfConsentIsAskedSeparately) {
    std::set<std::string_view> named;
    for (const auto kind : kEveryConsentKind) {
        ASSERT_TRUE(Give(kAdult, kAdult, kind).HasValue()) << Name(kind);
        named.insert(Name(kind));
    }

    EXPECT_EQ(named.size(), kEveryConsentKind.size()) << "два вида названы одинаково";
    EXPECT_EQ(records_.Size(), kEveryConsentKind.size());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: существенная новая версия требует повторного
/// принятия, косметическая — нет.
TEST_F(ConsentTest, ASubstantialVersionAsksAgainAndACosmeticOneDoesNot) {
    ASSERT_TRUE(Give(kAdult, kAdult).HasValue());
    ASSERT_FALSE(Showing().Execute(kTenant, kAdult).Value().asks_to_accept_again);

    versions_.Release(2, VersionChange::kCosmetic);
    EXPECT_FALSE(Showing().Execute(kTenant, kAdult).Value().asks_to_accept_again)
        << "поправили формулировку — и спросили согласие заново";

    versions_.Release(3, VersionChange::kSubstantial);
    EXPECT_TRUE(Showing().Execute(kTenant, kAdult).Value().asks_to_accept_again)
        << "появилась новая категория или новый получатель — и не спросили";
}

/// Существенность назначает человек, а не выводит код: две версии подряд с
/// одним номером изменений дают разный ответ только потому, что их назвали
/// по-разному.
TEST_F(ConsentTest, SignificanceIsToldNotGuessed) {
    ASSERT_TRUE(Give(kAdult, kAdult).HasValue());

    versions_.Release(2, VersionChange::kCosmetic);
    const bool after_cosmetic = Showing().Execute(kTenant, kAdult).Value().asks_to_accept_again;

    versions_.Release(2, VersionChange::kSubstantial);
    const bool after_substantial = Showing().Execute(kTenant, kAdult).Value().asks_to_accept_again;

    EXPECT_NE(after_cosmetic, after_substantial)
        << "ответ не зависит от того, каким назвали изменение — значит, его угадывают";
}

TEST_F(ConsentTest, WithoutAnyConsentWeAskForIt) {
    const auto shown = Showing().Execute(kTenant, kAdult);

    ASSERT_TRUE(shown.HasValue());
    EXPECT_TRUE(shown.Value().asks_to_accept_again);
    EXPECT_TRUE(shown.Value().consents.empty());
}

/// Отзыв не стирает след: строка остаётся, и экран это показывает.
TEST_F(ConsentTest, AWithdrawnConsentStaysVisibleAndStopsCounting) {
    const auto given = Give(kAdult, kAdult);
    ASSERT_TRUE(given.HasValue());

    const auto withdrawn = given.Value().Withdrawn(world_.clock.Now());
    ASSERT_TRUE(withdrawn.HasValue()) << withdrawn.Failure().Code();
    records_.Save(withdrawn.Value());

    const auto shown = Showing().Execute(kTenant, kAdult);
    ASSERT_TRUE(shown.HasValue());

    ASSERT_EQ(shown.Value().consents.size(), 1U) << "отзыв стёр строку";
    EXPECT_FALSE(shown.Value().consents.front().live);
    EXPECT_TRUE(shown.Value().asks_to_accept_again)
        << "отозванное согласие продолжает считаться действующим";
}

TEST_F(ConsentTest, AConsentIsNotWithdrawnTwice) {
    const auto given = Give(kAdult, kAdult);
    ASSERT_TRUE(given.HasValue());

    const auto once = given.Value().Withdrawn(world_.clock.Now());
    ASSERT_TRUE(once.HasValue());
    const auto twice = once.Value().Withdrawn(world_.clock.Now());

    ASSERT_FALSE(twice.HasValue());
    EXPECT_EQ(twice.Failure().Code(), "consent_already_withdrawn");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: экран «мои данные» показывает ВСЕ категории из
/// перечня и всех получателей. Умолчавший о категории экран — это данные, о
/// которых человеку не сказали.
TEST_F(ConsentTest, MyDataShowsEveryCategoryAndEveryRecipient) {
    const auto shown = Showing().Execute(kTenant, kAdult);
    ASSERT_TRUE(shown.HasValue());

    EXPECT_EQ(shown.Value().categories.size(), kEveryPersonalDataCategory.size());
    for (const auto category : kEveryPersonalDataCategory) {
        EXPECT_NE(
            std::find(shown.Value().categories.begin(), shown.Value().categories.end(), category),
            shown.Value().categories.end())
            << "на экране нет категории «" << Name(category) << "»";
    }

    EXPECT_EQ(shown.Value().recipients.size(), kEveryRecipient.size());
    for (const auto recipient : kEveryRecipient) {
        EXPECT_NE(
            std::find(shown.Value().recipients.begin(), shown.Value().recipients.end(), recipient),
            shown.Value().recipients.end())
            << "на экране нет получателя «" << Name(recipient) << "»";
    }
}

TEST_F(ConsentTest, TheScreenNamesTheVersionInForce) {
    versions_.Release(7, VersionChange::kCosmetic);

    const auto shown = Showing().Execute(kTenant, kAdult);

    ASSERT_TRUE(shown.HasValue());
    EXPECT_EQ(shown.Value().current_version.Number(), 7);
}

TEST(PersonalData, CategoriesAndRecipientsAreTheWordsTheListKnows) {
    for (const auto category : kEveryPersonalDataCategory) {
        const auto parsed = ParsePersonalDataCategory(Name(category));
        ASSERT_TRUE(parsed.has_value()) << Name(category);
        EXPECT_EQ(*parsed, category);
    }
    for (const auto recipient : kEveryRecipient) {
        const auto parsed = ParseRecipient(Name(recipient));
        ASSERT_TRUE(parsed.has_value()) << Name(recipient);
        EXPECT_EQ(*parsed, recipient);
    }

    EXPECT_FALSE(ParsePersonalDataCategory("что-нибудь_ещё").has_value());
    EXPECT_FALSE(ParseRecipient("кто_нибудь_ещё").has_value());
}

TEST(PersonalData, VersionsStartAtOne) {
    EXPECT_FALSE(PolicyVersion::Of(0).HasValue());
    EXPECT_FALSE(PolicyVersion::Of(-1).HasValue());
    EXPECT_TRUE(PolicyVersion::Of(1).HasValue());
    EXPECT_LT(PolicyVersion::Of(1).Value(), PolicyVersion::Of(2).Value());
}

TEST(PersonalData, AConsentWithoutAnActionIsNotAConsent) {
    const auto nameless = ConsentRecord::Give(Numbered<ConsentRecordId>(1),
                                              kTenant,
                                              kAdult,
                                              kAdult,
                                              ConsentKind::kProcessing,
                                              PolicyVersion::Of(1).Value(),
                                              ConsentAction::kBoundary,
                                              core::Instant::FromUnixMicros(0));

    ASSERT_FALSE(nameless.HasValue());
    EXPECT_EQ(nameless.Failure().Code(), "consent_action_unknown");
}

}  // namespace pdr::identity
