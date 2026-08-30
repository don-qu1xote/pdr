#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "application/ports/secret_generator.hpp"
#include "core/types/ids.hpp"

namespace pdr::testing {

/// Единственный фейковый источник секретов проекта.
///
/// Выдаёт подряд идущие значения — как и фейковый генератор идентификаторов, и
/// ровно по той же причине: ожидаемый результат теста нужно уметь записать
/// руками, а случайные 256 бит так не записываются.
///
/// НАСТОЯЩЕЙ СЛУЧАЙНОСТИ ЗДЕСЬ НЕТ И НЕ ДОЛЖНО БЫТЬ. Предсказуемость — это то,
/// ради чего фейк существует; поэтому он и лежит в оснастке тестов, которая в
/// сервис не линкуется никогда (проверяется конфигурацией CMake).
class FakeSecretGenerator final : public application::ports::SecretGenerator {
public:
    explicit FakeSecretGenerator(std::uint64_t first = 1) noexcept;

    /// Длина та же, что у настоящего: base64url без набивки от `bytes` байт —
    /// это ceil(bytes * 4 / 3) знаков. Иначе фейк проходил бы там, где
    /// настоящий упирается в нижнюю границу длины токена.
    std::string NextText(std::size_t bytes) const override;

    /// Сколько секретов уже выдано.
    std::uint64_t Issued() const noexcept;

protected:
    core::IdBytes NextIdBytes() const override;

private:
    std::uint64_t first_;
    mutable std::uint64_t next_;
};

}  // namespace pdr::testing
