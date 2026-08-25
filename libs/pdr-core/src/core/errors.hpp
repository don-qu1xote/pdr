#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace pdr::core {

/// Род отказа. Список закрытый и короткий, потому что отсюда — и только
/// отсюда — доменная ошибка один раз отображается в HTTP (problem+json).
/// Подробность несёт код ошибки, а не новый род.
enum class ErrorKind : std::uint8_t {
    kValidation,  ///< данные не проходят правило домена
    kNotFound,    ///< того, о чём спрашивают, нет
    kConflict,  ///< состояние не позволяет: слот занят, связь уже отозвана
    kForbidden,  ///< роль не даёт права на это действие
};

std::string_view Name(ErrorKind kind) noexcept;

/// Ожидаемый отказ — значение, которое возвращают.
///
/// Исключения остаются для того, что чинит программист: битые данные в
/// хранилище, недостижимый инвариант, ошибка в коде. «Слот занят» и «связь уже
/// отозвана» — не аварии, а обычные ответы домена, и бросать их значит терять
/// их по дороге: catch ловит всё сразу, а обработчик перестаёт различать.
class Error final {
public:
    Error(ErrorKind kind, std::string code, std::string detail = {})
        : kind_{kind}, code_{std::move(code)}, detail_{std::move(detail)} {}

    ErrorKind Kind() const noexcept {
        return kind_;
    }

    /// Стабильный машинный код: «slot_already_taken». По нему клиент различает
    /// отказы, поэтому он не меняется вместе с текстом.
    const std::string& Code() const noexcept {
        return code_;
    }

    /// Подробность для человека. Не для разбора машиной и не для показа ученику
    /// без перевода на язык интерфейса.
    const std::string& Detail() const noexcept {
        return detail_;
    }

    friend bool operator==(const Error&, const Error&) = default;

private:
    ErrorKind kind_;
    std::string code_;
    std::string detail_;
};

/// Либо значение, либо отказ. Ничего третьего и никаких «пустых» состояний.
///
/// Конструкторы намеренно неявные: `return money;` и `return error;` читаются
/// как обычный возврат, а отказ из вложенного вызова пробрасывается наружу
/// одной строкой — `return priced.Failure();` — и не теряется по пути.
template<class T>
class Result final {
public:
    Result(T value) : state_{std::move(value)} {}
    Result(Error error) : state_{std::move(error)} {}

    bool HasValue() const noexcept {
        return std::holds_alternative<T>(state_);
    }

    explicit operator bool() const noexcept {
        return HasValue();
    }

    /// Обращение не к тому состоянию — ошибка программиста, а не отказ домена,
    /// поэтому здесь исключение.
    const T& Value() const {
        if (!HasValue()) {
            throw std::logic_error{"Result::Value() у результата с ошибкой: " + Failure().Code()};
        }
        return std::get<T>(state_);
    }

    const Error& Failure() const {
        if (HasValue()) {
            throw std::logic_error{"Result::Failure() у результата со значением"};
        }
        return std::get<Error>(state_);
    }

private:
    std::variant<T, Error> state_;
};

/// Сценарий, которому нечего вернуть в случае успеха: «отозвать связь»,
/// «отменить занятие». Успех — это `return {};`.
template<>
class Result<void> final {
public:
    Result() noexcept = default;
    Result(Error error) : error_{std::move(error)} {}

    bool HasValue() const noexcept {
        return !error_.has_value();
    }

    explicit operator bool() const noexcept {
        return HasValue();
    }

    const Error& Failure() const {
        if (HasValue()) {
            throw std::logic_error{"Result<void>::Failure() у успешного результата"};
        }
        return *error_;
    }

private:
    std::optional<Error> error_;
};

}  // namespace pdr::core
