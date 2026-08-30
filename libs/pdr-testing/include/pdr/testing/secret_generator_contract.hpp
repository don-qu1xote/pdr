#pragma once

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "application/ports/secret_generator.hpp"
#include "core/types/ids.hpp"

/// @file
/// Contract-набор порта секретов: ОДИН набор проверок для фейка и для
/// настоящего источника случайности.
///
/// Фейк выдаёт подряд идущие значения, настоящий — байты от криптографического
/// источника. Разными они быть обязаны, а вот в трёх вещах — нет: секреты не
/// повторяются, читаются обратно и имеют ту длину, на которую рассчитывает
/// домен. Фейк, нарушающий это, делает зелёными тесты, которые в проде
/// оказываются входом без пароля.
///
/// ЧЕГО ЭТОТ НАБОР НЕ ПРОВЕРЯЕТ И НЕ МОЖЕТ: непредсказуемость. Отличить
/// хороший источник от плохого набором тестов нельзя — `std::mt19937` пройдёт
/// его целиком. Непредсказуемость обеспечивается ТИПОМ: сценарий входа просит
/// `SecretGenerator&`, и обычный генератор идентификаторов в этот параметр не
/// подставляется.
///
/// Требования к «миру»:
///
/// @code
/// struct MySecretWorld {
///     const application::ports::SecretGenerator& Secrets();
/// };
/// @endcode

namespace pdr::testing {

template<class World>
class SecretGeneratorContract : public ::testing::Test {
protected:
    World world_;
};

TYPED_TEST_SUITE_P(SecretGeneratorContract);

TYPED_TEST_P(SecretGeneratorContract, IdentifiersDoNotRepeat) {
    const auto& secrets = this->world_.Secrets();

    std::set<std::string> seen;
    for (int issued = 0; issued < 100; ++issued) {
        seen.insert(secrets.template Next<core::PersonId>().ToString());
    }

    EXPECT_EQ(seen.size(), 100U) << "источник секретов повторился на сотне значений";
}

TYPED_TEST_P(SecretGeneratorContract, TextsDoNotRepeat) {
    const auto& secrets = this->world_.Secrets();

    std::set<std::string> seen;
    for (int issued = 0; issued < 100; ++issued) {
        seen.insert(secrets.NextText(32));
    }

    EXPECT_EQ(seen.size(), 100U) << "две одноразовые ссылки совпали";
}

/// Длина текста — не мелочь: домен отвергает секрет короче 43 знаков, и фейк,
/// выдающий короткое, прятал бы этот отказ до самого прода.
TYPED_TEST_P(SecretGeneratorContract, TextIsLongEnoughForTheDomain) {
    const auto& secrets = this->world_.Secrets();

    EXPECT_GE(secrets.NextText(32).size(), 43U);
    EXPECT_GE(secrets.NextText(16).size(), 21U);
}

TYPED_TEST_P(SecretGeneratorContract, TextIsSafeInsideALink) {
    const auto& secrets = this->world_.Secrets();

    const auto text = secrets.NextText(32);
    for (const char symbol : text) {
        const bool allowed = (symbol >= '0' && symbol <= '9') || (symbol >= 'a' && symbol <= 'z') ||
                             (symbol >= 'A' && symbol <= 'Z') || symbol == '-' || symbol == '_';
        EXPECT_TRUE(allowed) << "знак «" << symbol << "» портится по дороге в адресной строке";
    }
}

TYPED_TEST_P(SecretGeneratorContract, IdentifierTextRoundTripsBack) {
    const auto& secrets = this->world_.Secrets();

    const auto issued = secrets.template Next<core::PersonId>();
    const auto parsed = core::PersonId::Parse(issued.ToString());

    ASSERT_TRUE(parsed.has_value()) << "выданный секрет не разбирается обратно";
    EXPECT_TRUE(*parsed == issued);
}

REGISTER_TYPED_TEST_SUITE_P(SecretGeneratorContract,
                            IdentifiersDoNotRepeat,
                            TextsDoNotRepeat,
                            TextIsLongEnoughForTheDomain,
                            TextIsSafeInsideALink,
                            IdentifierTextRoundTripsBack);

}  // namespace pdr::testing

/// Инстанцировать набор на своём «мире».
#define PDR_SECRET_GENERATOR_CONTRACT(prefix, world) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, SecretGeneratorContract, ::testing::Types<world>)
