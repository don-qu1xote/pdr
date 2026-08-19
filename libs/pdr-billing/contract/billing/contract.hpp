#pragma once

#include <string_view>

#include "core/errors.hpp"
#include "core/money.hpp"

namespace pdr::billing {

/// Публичный контракт контекста billing — единственный его заголовок наружу.
///
/// Код тарифа приходит сюда сырой строкой, а не доменным значением: чужой
/// контекст не обязан знать правила разбора, и они не должны застыть в его
/// коде. Разбирает и отвергает неверное сам billing — на своей границе.
///
/// Арендатора в подписи нет намеренно: его задаёт контур соединения с базой
/// (RLS), а не аргумент, который вызывающий может забыть или подменить.
class Contract {
public:
    Contract(const Contract&) = delete;
    Contract& operator=(const Contract&) = delete;

    virtual ~Contract() = default;

    /// Сколько стоит пакет из `lessons` занятий по тарифу с кодом `tariff_code`.
    virtual core::Result<core::Money> QuotePackage(std::string_view tariff_code,
                                                   int lessons) const = 0;

protected:
    Contract() = default;
};

}  // namespace pdr::billing
