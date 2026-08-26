#pragma once

#include <cstddef>
#include <string>

#include "core/types/ids.hpp"

namespace pdr::application::ports {

/// Случайность, от которой зависит, войдёт ли посторонний.
///
/// ЭТО НЕ ВТОРОЙ `IdGenerator`, и разница не в словах. Обычный генератор
/// идентификаторов выдаёт значения, которые достаточно не путать между собой;
/// здесь значение обязано быть НЕПРЕДСКАЗУЕМЫМ. `std::mt19937`, на котором
/// стоит `infrastructure::RandomIdGenerator`, восстанавливается по нескольким
/// сотням выданных значений целиком — то есть следующий идентификатор сессии
/// вычисляется, а не угадывается.
///
/// Порт отдельный именно затем, чтобы перепутать было НЕЧЕМ: сценарий входа
/// просит `SecretGenerator&`, и обычный генератор в этот параметр не
/// подставляется — это ошибка компиляции, а не недосмотр на ревью.
///
/// Реализация обязана брать байты у криптографического источника операционной
/// системы. Своего перемешивания здесь не пишут: `infrastructure::CryptoSecretGenerator`
/// зовёт штатный `crypto::GenerateRandomBlock` userver.
class SecretGenerator {
public:
    SecretGenerator(const SecretGenerator&) = delete;
    SecretGenerator& operator=(const SecretGenerator&) = delete;

    virtual ~SecretGenerator() = default;

    /// Непредсказуемый типизированный идентификатор — тем же способом, что и у
    /// обычного генератора: тип навешивается снаружи.
    template<class Id>
    Id Next() const {
        static_assert(core::kIsStrongId<Id>,
                      "Next() выдаёт только типизированные идентификаторы: Next<SessionId>()");
        return Id::FromBytes(NextIdBytes());
    }

    /// Непредсказуемая строка из `bytes` байт для одноразовой ссылки.
    ///
    /// Кодировка — base64url без набивки: ссылку человек пересылает в
    /// мессенджере и вставляет в адресную строку, а `+`, `/` и `=` там
    /// портятся молча.
    virtual std::string NextText(std::size_t bytes) const = 0;

protected:
    SecretGenerator() = default;

    virtual core::IdBytes NextIdBytes() const = 0;
};

}  // namespace pdr::application::ports
