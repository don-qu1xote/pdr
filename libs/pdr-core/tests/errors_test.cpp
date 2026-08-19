#include "core/errors.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "application/quote_lesson_package.hpp"
#include "core/lesson_package.hpp"
#include "core/money.hpp"
#include "core/tariff.hpp"
#include "testing/check.hpp"

namespace {

using pdr::application::QuoteLessonPackage;
using pdr::core::Error;
using pdr::core::ErrorKind;
using pdr::core::Money;
using pdr::core::Result;
using pdr::core::Tariff;
using pdr::core::TariffCode;

/// Фейк узкого порта: четыре строки, потому что порт узкий.
class FakeTariffRepository final : public pdr::application::ports::TariffRepository {
public:
    explicit FakeTariffRepository(std::optional<Tariff> tariff) : tariff_{std::move(tariff)} {}

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
    const auto refused = pdr::core::PackagePrice(MakeTariff(), 0);

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(!static_cast<bool>(refused));
    PDR_CHECK(refused.Failure().Kind() == ErrorKind::kValidation);
    PDR_CHECK(refused.Failure().Code() == "lessons_not_positive");
    PDR_CHECK(!refused.Failure().Detail().empty());

    const auto priced = pdr::core::PackagePrice(MakeTariff(), 8);
    PDR_CHECK(priced.HasValue());
    PDR_CHECK(priced.Value().MinorUnits() == 2000000);
}

void RefusalSurvivesTheWayOut() {
    const FakeTariffRepository tariffs{MakeTariff()};
    const QuoteLessonPackage quote{tariffs};

    // Отказ пришёл из домена и вышел из сценария тем же самым значением:
    // ни рода, ни кода, ни подробности по дороге не потеряли и не переписали.
    const auto from_domain = pdr::core::PackagePrice(MakeTariff(), -1);
    const auto from_scenario = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), -1});

    PDR_CHECK(!from_scenario.HasValue());
    PDR_CHECK(from_scenario.Failure() == from_domain.Failure());

    const auto priced = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), 8});
    PDR_CHECK(priced.HasValue());
    PDR_CHECK(priced.Value().MinorUnits() == 2000000);
}

void MissingTariffIsNotFoundAndNotAnException() {
    const FakeTariffRepository empty{std::nullopt};
    const QuoteLessonPackage quote{empty};

    const auto answer = quote.Execute({*TariffCode::Parse("MATH-EGE-90"), 8});
    PDR_CHECK(!answer.HasValue());
    PDR_CHECK(answer.Failure().Kind() == ErrorKind::kNotFound);
    PDR_CHECK(answer.Failure().Code() == "tariff_not_found");
    PDR_CHECK(pdr::core::Name(answer.Failure().Kind()) == "not_found");
}

void ScenarioWithoutPayloadAlsoReturnsRefusal() {
    const Result<void> done;
    PDR_CHECK(done.HasValue());

    const Result<void> refused{
        Error{ErrorKind::kConflict, "link_already_revoked", "связь с опекуном уже отозвана"}};
    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Kind() == ErrorKind::kConflict);
    PDR_CHECK(refused.Failure().Code() == "link_already_revoked");
}

/// Обращение не к тому состоянию — ошибка программиста, и вот она как раз
/// исключение: чинить её будет тот, кто написал вызов.
void WrongStateIsAProgrammersMistake() {
    bool value_threw = false;
    try {
        const auto refused = pdr::core::PackagePrice(MakeTariff(), 0);
        (void)refused.Value();
    } catch (const std::logic_error&) {
        value_threw = true;
    }
    PDR_CHECK(value_threw);

    bool failure_threw = false;
    try {
        const auto priced = pdr::core::PackagePrice(MakeTariff(), 1);
        (void)priced.Failure();
    } catch (const std::logic_error&) {
        failure_threw = true;
    }
    PDR_CHECK(failure_threw);
}

}  // namespace

int main() {
    DomainRefusalIsAValue();
    RefusalSurvivesTheWayOut();
    MissingTariffIsNotFoundAndNotAnException();
    ScenarioWithoutPayloadAlsoReturnsRefusal();
    WrongStateIsAProgrammersMistake();
    return pdr::testing::Summary("core.errors");
}
