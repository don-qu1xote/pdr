#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace pdr::infrastructure::db {

/// Значение колонки, которую схема объявила not null.
///
/// Порождённые структуры строк складывают КАЖДУЮ колонку в `std::optional`:
/// разбор запроса не знает про `not null` и на всякий случай допускает пустоту.
/// Разворачивать её через `value()` значит терять имя колонки в отчёте —
/// «bad optional access» не говорит, где схема разошлась с данными.
template<typename T>
const T& Filled(const std::optional<T>& value, const char* column) {
    if (!value.has_value()) {
        throw std::runtime_error{std::string{"колонка «"} + column +
                                 "» пришла пустой, хотя схема этого не допускает"};
    }
    return *value;
}

}  // namespace pdr::infrastructure::db
