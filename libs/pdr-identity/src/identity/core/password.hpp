#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"

namespace pdr::identity {

/// Как считается хеш пароля и какой пароль вообще принимается.
///
/// Значения приходят из динамического конфига (`PDR_SIGN_IN_RULES`), а не из
/// констант: стоимость счёта подбирают под живое железо, и подбирать её
/// перевыкаткой — значит не подбирать вовсе.
///
/// Схема реестра задаёт пределы каждой величины по отдельности. Связь между
/// ними схемой не выражается, и её проверяет здесь домен: параллельность выше
/// памяти Argon2 не принимает вовсе, а негодная запись обязана отвергаться
/// целиком — со старыми значениями на месте.
class PasswordRules final {
public:
    static core::Result<PasswordRules> Compose(std::uint32_t memory_kib,
                                               std::uint32_t iterations,
                                               std::uint32_t parallelism,
                                               std::size_t min_length);

    std::uint32_t MemoryKib() const noexcept {
        return memory_kib_;
    }
    std::uint32_t Iterations() const noexcept {
        return iterations_;
    }
    std::uint32_t Parallelism() const noexcept {
        return parallelism_;
    }
    /// В ЗНАКАХ, а не в байтах: иначе от русского пароля требовалось бы вдвое
    /// меньше знаков, чем от английского.
    std::size_t MinLength() const noexcept {
        return min_length_;
    }

    friend bool operator==(const PasswordRules&, const PasswordRules&) = default;

private:
    PasswordRules(std::uint32_t memory_kib,
                  std::uint32_t iterations,
                  std::uint32_t parallelism,
                  std::size_t min_length) noexcept
        : memory_kib_{memory_kib},
          iterations_{iterations},
          parallelism_{parallelism},
          min_length_{min_length} {}

    std::uint32_t memory_kib_;
    std::uint32_t iterations_;
    std::uint32_t parallelism_;
    std::size_t min_length_;
};

/// Пароль в открытом виде — ровно на время одного сценария.
///
/// Отдельный тип, а не `std::string`, и это не украшение: строка попадает в
/// журнал одним неосторожным `LOG_INFO() << value`, а этот тип нечем вывести —
/// у него нет ни оператора вывода, ни неявного преобразования. Достать
/// содержимое можно только назвав `Secret()`, и такой вызов виден на ревью.
///
/// Верхняя граница длины стоит не ради строгости: Argon2 честно считает хеш от
/// мегабайтного «пароля», и это способ занять сервер бесплатно.
class Password final {
public:
    static constexpr std::size_t kMaxLength = 256;

    /// Новый пароль — тот, который человек СЕБЕ ЗАДАЁТ. Проверяется правилами
    /// целиком: тут ему и место сказать «коротко».
    static core::Result<Password> Chosen(std::string_view text, const PasswordRules& rules);

    /// Пароль, введённый при входе. Правилами НЕ проверяется, и это не
    /// небрежность: у давнего человека пароль может быть короче нынешнего
    /// порога, а «слишком короткий» в ответ на вход рассказывает постороннему
    /// про наши правила и про то, что запись существует. Отвергается только
    /// длина, от которой Argon2 честно считает хеш мегабайта.
    static core::Result<Password> Given(std::string_view text);

    /// Единственный способ добраться до содержимого. Назван так, чтобы его было
    /// видно в диффе.
    const std::string& Secret() const noexcept {
        return secret_;
    }

private:
    explicit Password(std::string secret) noexcept : secret_{std::move(secret)} {}

    std::string secret_;
};

/// Хеш пароля в записи Argon2 (`$argon2id$v=19$m=...`).
///
/// Параметры счёта лежат ВНУТРИ самой записи, и это то, ради чего берётся
/// именно такая форма: сменили стоимость в конфиге — старые хеши продолжают
/// проверяться своими параметрами, а не новыми. Иначе смена стоимости означала
/// бы, что войти больше не может никто.
class PasswordHash final {
public:
    static core::Result<PasswordHash> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

    friend bool operator==(const PasswordHash&, const PasswordHash&) = default;

private:
    explicit PasswordHash(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

}  // namespace pdr::identity
