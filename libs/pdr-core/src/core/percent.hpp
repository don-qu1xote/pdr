#pragma once

#include <cstdint>
#include <optional>

#include "core/money.hpp"

namespace pdr::core {

/// Доля в процентах: целое от нуля до ста.
///
/// ЦЕЛОЕ, А НЕ ДРОБЬ, и не потому, что так проще. Доля удержания — число, о
/// котором договариваются репетитор и ученик и которое потом читают в споре;
/// «17,5 %» в такой договорённости не встречается, а число с плавающей точкой
/// в расчёте денег не встречается вовсе (docs/architecture/postulates.md).
/// Понадобится десятая доля процента — здесь появятся базисные пункты, и
/// поменяется этот тип, а не двадцать мест, где он посчитан руками.
class Percent final {
public:
    static constexpr int kWhole = 100;

    /// Доля вне ноль-ста не существует: «сто двадцать процентов удержания» —
    /// не строгая политика, а ошибка ввода.
    static std::optional<Percent> Compose(int value) noexcept;

    static Percent Nothing() noexcept;

    int Value() const noexcept {
        return value_;
    }

    /// Столько от суммы. ОКРУГЛЕНИЕ ВНИЗ, В ПОЛЬЗУ ПЛАТЯЩЕГО: остаток копейки
    /// достаётся тому, с кого удерживают, и правило это одно на все расчёты —
    /// иначе половина мест округляет так, половина иначе, и сходится это только
    /// в споре.
    ///
    /// Пусто при переполнении: минорные единицы умножаются на сто, и у больших
    /// сумм это перестаёт помещаться в целое. Отказ — обычный ответ, а не
    /// авария.
    std::optional<Money> Of(const Money& amount) const noexcept;

    friend bool operator==(const Percent&, const Percent&) = default;

private:
    explicit constexpr Percent(int value) noexcept : value_{value} {}

    int value_{0};
};

}  // namespace pdr::core
