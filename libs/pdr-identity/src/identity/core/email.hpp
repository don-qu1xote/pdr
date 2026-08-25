#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"

namespace pdr::identity {

/// Почта как значение, а не как строка.
///
/// Приводится к нижнему регистру при разборе: тогда «Ivan@Example.Ru» и
/// «ivan@example.ru» — один человек, а не два, и уникальность в схеме не
/// обходится сменой регистра (`identity_person_email_lowercase`).
///
/// Форма проверяется грубо и намеренно: единственная собака, непустые части
/// вокруг неё, точка в домене. Строгая проверка почты регулярным выражением —
/// известный способ отвергнуть настоящий адрес; окончательно проверяет письмо,
/// а не мы.
class Email final {
public:
    static core::Result<Email> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

    friend bool operator==(const Email&, const Email&) = default;

private:
    explicit Email(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

}  // namespace pdr::identity
