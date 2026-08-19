#include "core/errors.hpp"

#include <stdexcept>
#include <string>

#include "testing/check.hpp"

namespace {

using pdr::core::Error;
using pdr::core::ErrorKind;
using pdr::core::Result;

/// Внутренний расчёт: отказывает значением.
Result<int> Halved(int value) {
    if (value % 2 != 0) {
        return Error{ErrorKind::kValidation, "not_even", "нечётное пополам не делится"};
    }
    return value / 2;
}

/// Тот, кто его вызывает, отказ не переписывает и не глотает.
Result<int> HalvedTwice(int value) {
    const auto once = Halved(value);
    if (!once.HasValue()) {
        return once.Failure();
    }
    return Halved(once.Value());
}

void RefusalIsAValueNotAnException() {
    const auto refused = Halved(3);

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(!static_cast<bool>(refused));
    PDR_CHECK(refused.Failure().Kind() == ErrorKind::kValidation);
    PDR_CHECK(refused.Failure().Code() == "not_even");
    PDR_CHECK(!refused.Failure().Detail().empty());

    const auto value = Halved(8);
    PDR_CHECK(value.HasValue());
    PDR_CHECK(static_cast<bool>(value));
    PDR_CHECK(value.Value() == 4);
}

void RefusalSurvivesTheWayOut() {
    const auto deep = Halved(3);
    const auto out = HalvedTwice(3);

    PDR_CHECK(!out.HasValue());
    // Тот же самый отказ, а не «что-то пошло не так» на два этажа выше.
    PDR_CHECK(out.Failure() == deep.Failure());

    const auto second_floor = HalvedTwice(6);
    PDR_CHECK(!second_floor.HasValue());
    PDR_CHECK(second_floor.Failure().Code() == "not_even");

    PDR_CHECK(HalvedTwice(8).Value() == 2);
}

void KindHasAStableName() {
    PDR_CHECK(pdr::core::Name(ErrorKind::kValidation) == "validation");
    PDR_CHECK(pdr::core::Name(ErrorKind::kNotFound) == "not_found");
    PDR_CHECK(pdr::core::Name(ErrorKind::kConflict) == "conflict");
    PDR_CHECK(pdr::core::Name(ErrorKind::kForbidden) == "forbidden");
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
        (void)Halved(3).Value();
    } catch (const std::logic_error&) {
        value_threw = true;
    }
    PDR_CHECK(value_threw);

    bool failure_threw = false;
    try {
        (void)Halved(4).Failure();
    } catch (const std::logic_error&) {
        failure_threw = true;
    }
    PDR_CHECK(failure_threw);
}

}  // namespace

int main() {
    RefusalIsAValueNotAnException();
    RefusalSurvivesTheWayOut();
    KindHasAStableName();
    ScenarioWithoutPayloadAlsoReturnsRefusal();
    WrongStateIsAProgrammersMistake();
    return pdr::testing::Summary("core.errors");
}
