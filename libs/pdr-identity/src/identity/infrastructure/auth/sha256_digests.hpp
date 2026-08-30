#pragma once

#include <string_view>

#include "identity/application/ports/digests.hpp"

namespace pdr::identity {

/// Отпечаток штатным `crypto::hash::Sha256` userver.
///
/// Своего SHA-256 в дереве нет и не будет: криптографическая примитивная
/// функция, написанная руками, выглядит работающей ровно до дня, когда её
/// сравнивают с эталоном (ADR-0013).
///
/// Регистр приводится к нижнему здесь, а не подразумевается: `Digest` принимает
/// только строчные знаки, и два написания одного отпечатка означали бы две
/// строки в базе и один ненайденный токен.
class Sha256Digests final : public ports::Digests {
public:
    Sha256Digests() = default;

    Digest Of(std::string_view text) const override;
};

}  // namespace pdr::identity
