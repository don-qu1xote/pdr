#pragma once

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "application/ports/id_generator.hpp"
#include "core/types/ids.hpp"

/// @file
/// Contract-набор порта генератора идентификаторов: ОДИН набор проверок для
/// фейка и для настоящего генератора.
///
/// Фейк выдаёт подряд идущие значения, настоящий — случайные шестнадцать байт.
/// Разными они быть обязаны, а вот в трёх вещах — нет: идентификаторы не
/// повторяются, читаются обратно из текста и остаются типизированными. Если фейк
/// это нарушает, все тесты поверх него врут.
///
/// Требования к «миру»:
///
/// @code
/// struct MyGeneratorWorld {
///     const application::ports::IdGenerator& Generator();
/// };
/// @endcode

namespace pdr::testing {

template<class World>
class IdGeneratorContract : public ::testing::Test {
protected:
    World world_;
};

TYPED_TEST_SUITE_P(IdGeneratorContract);

/// Выданное дважды не совпадает. Повторяющийся идентификатор — это две записи,
/// склеенные в одну, и заметно это станет на чужом занятии.
TYPED_TEST_P(IdGeneratorContract, IdentifiersDoNotRepeat) {
    const auto& generator = this->world_.Generator();

    std::set<std::string> seen;
    for (int issued = 0; issued < 100; ++issued) {
        seen.insert(generator.template Next<core::PersonId>().ToString());
    }

    EXPECT_EQ(seen.size(), 100U) << "генератор повторился на сотне значений";
}

/// Идентификатор читается обратно: текст, отданный наружу, разбирается в то же
/// значение. Иначе идентификатор из ответа API нельзя вернуть в запросе.
TYPED_TEST_P(IdGeneratorContract, TextRoundTripsBack) {
    const auto& generator = this->world_.Generator();

    const auto issued = generator.template Next<core::PersonId>();
    const auto parsed = core::PersonId::Parse(issued.ToString());

    ASSERT_TRUE(parsed.has_value()) << "выданный идентификатор не разбирается обратно";
    EXPECT_TRUE(*parsed == issued);
}

/// Разные сущности получают разные значения из одного генератора: последователь-
/// ность общая, а не по счётчику на тип.
TYPED_TEST_P(IdGeneratorContract, DifferentEntitiesDoNotShareValues) {
    const auto& generator = this->world_.Generator();

    const auto person = generator.template Next<core::PersonId>();
    const auto tenant = generator.template Next<core::TenantId>();

    EXPECT_NE(person.ToString(), tenant.ToString());
}

REGISTER_TYPED_TEST_SUITE_P(IdGeneratorContract,
                            IdentifiersDoNotRepeat,
                            TextRoundTripsBack,
                            DifferentEntitiesDoNotShareValues);

}  // namespace pdr::testing

#define PDR_ID_GENERATOR_CONTRACT(prefix, world) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, IdGeneratorContract, ::testing::Types<world>)
