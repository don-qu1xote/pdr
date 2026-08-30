#pragma once

#include <cstddef>
#include <string>

#include "application/ports/secret_generator.hpp"
#include "core/types/ids.hpp"

namespace pdr::infrastructure {

/// Настоящая случайность: штатный `crypto::GenerateRandomBlock` userver, за
/// которым стоит CryptoPP::AutoSeededRandomPool.
///
/// Своего перемешивания, своего пула и своего «подмешаем время» здесь нет и не
/// будет: генератор случайности — ровно тот случай, где самоделка выглядит
/// работающей до дня, когда кто-то посчитает её выход (ADR-0013).
class CryptoSecretGenerator final : public application::ports::SecretGenerator {
public:
    CryptoSecretGenerator() = default;

    std::string NextText(std::size_t bytes) const override;

private:
    core::IdBytes NextIdBytes() const override;
};

}  // namespace pdr::infrastructure
