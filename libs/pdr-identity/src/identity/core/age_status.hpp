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

    /// МОМЕНТ, В КОТОРЫЙ ЧЕЛОВЕКУ ИСПОЛНЯЕТСЯ СТОЛЬКО ЛЕТ.
    ///
    /// Нужен там, где от совершеннолетия отсчитывают срок: окно на решение о
    /// родительском доступе начинается не «когда заметили», а в день рождения.
    ///
    /// Считается полночь UTC того дня. Зона человека тут не учитывается
    /// намеренно: перевод в местное время — работа адаптера с базой зон, а
    /// разница в несколько часов ничего не меняет для окна длиной в недели.
    ///
    /// Двадцать девятое февраля превращается в первое марта: в невисокосный год
    /// такого дня нет, и совершеннолетие наступает на следующий день после
    /// последнего дня февраля.
    static core::Instant TurnsAt(const BirthDate& born_on, int years) noexcept;

private:
    AgeStatus(BirthDate born_on, core::Instant moment, int years) noexcept
        : born_on_{born_on}, moment_{moment}, years_{years} {}

    BirthDate born_on_;
    core::Instant moment_;
    int years_{0};
};

}  // namespace pdr::identity
