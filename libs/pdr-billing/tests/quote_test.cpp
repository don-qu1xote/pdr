#include <optional>
#include <utility>

#include "billing/application/contract_service.hpp"
#include "billing/application/quote_lesson_package.hpp"
#include "billing/contract.hpp"
#include "billing/core/lesson_package.hpp"
#include "core/errors.hpp"
#include "core/money.hpp"
#include "testing/check.hpp"

namespace {

using pdr::billing::QuoteLessonPackage;
using pdr::billing::Tariff;
using pdr::billing::TariffCode;
using pdr::core::ErrorKind;
using pdr::core::Money;

class FakeTariffs final : public pdr::billing::ports::TariffRepository {
public:
    explicit FakeTariffs(std::optional<Tariff> tariff) : tariff_{std::move(tariff)} {}

    std::optional<Tariff> FindByCode(const TariffCode&) const override {
        return tariff_;
    }

private:
    std::optional<Tariff> tariff_;
};

Tariff MakeTariff() {
    const auto currency = pdr::core::CurrencyCode::Parse("RUB");
    const auto code = TariffCode::Parse("MATH-EGE-90");
    return Tariff{*code, Money::FromMinorUnits(250000, *currency)};
}

void DomainRefusalIsAValue() {
    const auto refused = pdr::billing::PackagePrice(MakeTariff(), 0);

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Kind() == ErrorKind::kValidation);
    PDR_CHECK(refused.Failure().Code() == "lessons_not_positive");

    const auto priced = pdr::billing::PackagePrice(MakeTariff(), 8);
    PDR_CHECK(priced.HasValue());
    PDR_CHECK(priced.Value().MinorUnits() == 2000000);
}

void RefusalSurvivesTheWayOut() {
    const FakeTariffs tariffs{MakeTariff()};
    const QuoteLessonPackage quote{tariffs};

    const auto from_domain = pdr::billing::PackagePrice(MakeTariff(), -1);
    const auto from_scenario = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), -1});

    PDR_CHECK(!from_scenario.HasValue());
    PDR_CHECK(from_scenario.Failure() == from_domain.Failure());

    const auto priced = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), 8});
    PDR_CHECK(priced.HasValue());
    PDR_CHECK(priced.Value().MinorUnits() == 2000000);
}

void MissingTariffIsNotFound() {
    const FakeTariffs empty{std::nullopt};
    const QuoteLessonPackage quote{empty};

    const auto answer = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), 8});

    PDR_CHECK(!answer.HasValue());
    PDR_CHECK(answer.Failure().Kind() == ErrorKind::kNotFound);
    PDR_CHECK(answer.Failure().Code() == "tariff_not_found");
}

void ContractParsesRawValuesAtItsOwnBorder() {
    const FakeTariffs tariffs{MakeTariff()};
    const pdr::billing::ContractService service{tariffs};
    const pdr::billing::Contract& contract = service;

    const auto priced = contract.QuotePackage("MATH-EGE-90", 4);
    PDR_CHECK(priced.HasValue());
    PDR_CHECK(priced.Value().MinorUnits() == 1000000);

    // Чужой контекст присылает строку — разбираем и отвергаем здесь, а не там.
    const auto refused = contract.QuotePackage("не код", 4);
    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Kind() == ErrorKind::kValidation);
    PDR_CHECK(refused.Failure().Code() == "tariff_code_invalid");
}

}  // namespace

int main() {
    DomainRefusalIsAValue();
    RefusalSurvivesTheWayOut();
    MissingTariffIsNotFound();
    ContractParsesRawValuesAtItsOwnBorder();
    return pdr::testing::Summary("billing.quote");
}
