#include "core/errors.hpp"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace pdr::core {
namespace {

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

TEST(Result, RefusalIsAValueNotAnException) {
    const auto refused = Halved(3);

    EXPECT_FALSE(refused.HasValue());
    EXPECT_FALSE(static_cast<bool>(refused));
    EXPECT_EQ(refused.Failure().Kind(), ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "not_even");
    EXPECT_FALSE(refused.Failure().Detail().empty());

    const auto value = Halved(8);
    ASSERT_TRUE(value.HasValue());
    EXPECT_TRUE(static_cast<bool>(value));
    EXPECT_EQ(value.Value(), 4);
}

TEST(Result, RefusalSurvivesTheWayOut) {
    const auto deep = Halved(3);
    const auto out = HalvedTwice(3);

    ASSERT_FALSE(out.HasValue());
    EXPECT_TRUE(out.Failure() == deep.Failure());

    const auto second_floor = HalvedTwice(6);
    ASSERT_FALSE(second_floor.HasValue());
    EXPECT_EQ(second_floor.Failure().Code(), "not_even");

    EXPECT_EQ(HalvedTwice(8).Value(), 2);
}

TEST(ErrorKind, HasAStableName) {
    EXPECT_EQ(Name(ErrorKind::kValidation), "validation");
    EXPECT_EQ(Name(ErrorKind::kNotFound), "not_found");
    EXPECT_EQ(Name(ErrorKind::kConflict), "conflict");
    EXPECT_EQ(Name(ErrorKind::kForbidden), "forbidden");
}

TEST(Result, ScenarioWithoutPayloadAlsoReturnsRefusal) {
    const Result<void> done;
    EXPECT_TRUE(done.HasValue());

    const Result<void> refused{
        Error{ErrorKind::kConflict, "link_already_revoked", "связь с опекуном уже отозвана"}};
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), ErrorKind::kConflict);
    EXPECT_EQ(refused.Failure().Code(), "link_already_revoked");
}

/// Обращение не к тому состоянию — ошибка программиста, и вот она как раз
/// исключение: чинить её будет тот, кто написал вызов.
TEST(Result, WrongStateIsAProgrammersMistake) {
    EXPECT_THROW((void)Halved(3).Value(), std::logic_error);
    EXPECT_THROW((void)Halved(4).Failure(), std::logic_error);
}

}  // namespace
}  // namespace pdr::core
