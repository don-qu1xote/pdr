#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"

namespace pdr::identity {

/// Отпечаток SHA-256 в шестнадцатеричной записи — значение, а не строка.
///
/// Отдельный тип нужен затем, чтобы отпечаток нельзя было перепутать с тем, от
/// чего он посчитан: `Digest` и `TokenSecret` не подставляются друг вместо
/// друга, и «положили в базу сам токен» — ошибка компиляции, а не находка
/// на ревью через полгода.
///
/// Самого счёта здесь нет и быть не может: SHA-256 живёт в userver, а домен
/// собирается без него. Считает адаптер (`ports::Digests`), домен только
/// хранит и сравнивает.
class Digest final {
public:
    /// Шестьдесят четыре знака от 0 до f. Верхний регистр отвергается, а не
    /// приводится: два написания одного отпечатка — это две строки в базе и
    /// один ненайденный токен.
    static core::Result<Digest> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

    friend bool operator==(const Digest&, const Digest&) = default;

private:
    explicit Digest(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

}  // namespace pdr::identity
