#pragma once

#include <string_view>

#include "identity/core/digest.hpp"

namespace pdr::identity::ports {

/// Счёт отпечатка SHA-256. Порт, потому что SHA-256 живёт в userver, а домен
/// собирается без него.
///
/// Соли здесь нет, и это осознанно. Соль защищает от словаря, а под отпечаток
/// попадают только значения с полной случайностью внутри — секрет ссылки,
/// строка клиента, адрес. Перебрать 256 бит случайности словарём нельзя, а
/// соль у токена сделала бы невозможным главное: найти строку по отпечатку
/// одним запросом.
class Digests {
public:
    Digests(const Digests&) = delete;
    Digests& operator=(const Digests&) = delete;

    virtual ~Digests() = default;

    virtual Digest Of(std::string_view text) const = 0;

protected:
    Digests() = default;
};

}  // namespace pdr::identity::ports
