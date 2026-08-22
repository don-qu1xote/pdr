#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "billing/application/contract_service.hpp"
#include "billing/application/quote_lesson_package.hpp"
#include "billing/contract.hpp"
#include "billing/core/lesson_package.hpp"
#include "builders/tariff_builder.hpp"
#include "core/errors.hpp"
#include "core/money.hpp"

namespace pdr::billing {
namespace {

using pdr::billing::testing::TariffBuilder;
using pdr::core::ErrorKind;

class FakeTariffs final : public ports::TariffRepository {
public:
    explicit FakeTariffs(std::optional<Tariff> tariff) : tariff_{std::move(tariff)} {}

    std::optional<Tariff> FindByCode(const TariffCode&) const override {
        return tariff_;
    }

private:
    std::optional<Tariff> tariff_;
};

TEST(PackagePrice, DomainRefusalIsAValue) {
    const auto tariff = TariffBuilder{}.PerLesson(2500).Build();

    const auto refused = PackagePrice(tariff, 0);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "lessons_not_positive");

    const auto priced = PackagePrice(tariff, 8);
    ASSERT_TRUE(priced.HasValue());
    EXPECT_EQ(priced.Value().MinorUnits(), 2000000);
}

TEST(QuoteLessonPackage, RefusalSurvivesTheWayOut) {
    const auto tariff = TariffBuilder{}.PerLesson(2500).Build();
    const FakeTariffs tariffs{tariff};
    const QuoteLessonPackage quote{tariffs};

    const auto from_domain = PackagePrice(tariff, -1);
    const auto from_scenario = quote.Execute({tariff.Code(), -1});

    ASSERT_FALSE(from_scenario.HasValue());
    EXPECT_TRUE(from_scenario.Failure() == from_domain.Failure());

    const auto priced = quote.Execute({tariff.Code(), 8});
    ASSERT_TRUE(priced.HasValue());
    EXPECT_EQ(priced.Value().MinorUnits(), 2000000);
}

TEST(QuoteLessonPackage, MissingTariffIsNotFound) {
    const FakeTariffs empty{std::nullopt};
    const QuoteLessonPackage quote{empty};

    const auto answer = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), 8});

    ASSERT_FALSE(answer.HasValue());
    EXPECT_EQ(answer.Failure().Kind(), ErrorKind::kNotFound);
    EXPECT_EQ(answer.Failure().Code(), "tariff_not_found");
}

TEST(BillingContract, ParsesRawValuesAtItsOwnBorder) {
    const FakeTariffs tariffs{TariffBuilder{}.PerLesson(2500).Build()};
    const ContractService service{tariffs};
    const Contract& contract = service;

    const auto priced = contract.QuotePackage("MATH-EGE-90", 4);
    ASSERT_TRUE(priced.HasValue());
    EXPECT_EQ(priced.Value().MinorUnits(), 1000000);

    // Чужой контекст присылает строку — разбираем и отвергаем здесь, а не там.
    const auto refused = contract.QuotePackage("не код", 4);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "tariff_code_invalid");
}

}  // namespace
}  // namespace pdr::billing
