#pragma once

#include "core/errors.hpp"
#include "core/types/time.hpp"
#include "identity/core/birth_date.hpp"

namespace pdr::identity {

/// Сколько человеку лет В МОМЕНТ, а не вообще.
///
/// Момент приходит снаружи — от порта часов (`application::ports::Clock`),
/// который домен не зовёт сам: иначе тест про день рождения зависел бы от того,
/// в какую секунду его запустили. Права меняются сами не потому, что кто-то
/// пересчитал поле, а потому, что поля нет: возраст спрашивают заново на каждый
/// момент.
///
/// ПОРОГОВ ЗДЕСЬ НЕТ. Ни четырнадцати, ни шестнадцати, ни восемнадцати: это
/// продуктовые числа, они живут в динамическом конфиге и меняются без выкатки
/// (`PDR_SELF_ACCOUNT_AGE`, читает `PDR-IDENT-05`). Тип отвечает на вопрос
/// «дотянул ли до такого-то порога», а сам порог получает параметром.
///
/// Булева поля «несовершеннолетний» нет и не будет: оно устаревает в полночь.
class AgeStatus final {
public:
    static core::Result<AgeStatus> At(BirthDate born_on, core::Instant moment);

    int Years() const noexcept {
        return years_;
    }
    const BirthDate& BornOn() const noexcept {
        return born_on_;
    }
    core::Instant Moment() const noexcept {
        return moment_;
    }

    /// Порог приходит снаружи — из динамического конфига, а не из этого файла.
    bool Reached(int threshold_years) const noexcept {
        return years_ >= threshold_years;
    }

    friend bool operator==(const AgeStatus&, const AgeStatus&) = default;

private:
    AgeStatus(BirthDate born_on, core::Instant moment, int years) noexcept
        : born_on_{born_on}, moment_{moment}, years_{years} {}

    BirthDate born_on_;
    core::Instant moment_;
    int years_{0};
};

}  // namespace pdr::identity
