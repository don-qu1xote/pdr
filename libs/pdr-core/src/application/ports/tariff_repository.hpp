#pragma once

#include <optional>

#include "billing/core/tariff.hpp"

namespace pdr::billing::ports {

/// Порт объявлен здесь, в application, а реализован в infrastructure — ради
/// этого разворота зависимости всё и затевалось: сценарий не знает, лежит тариф
/// в Postgres, в чужом API или в фейке из теста.
///
/// Порт узкий: один вопрос, который сценарий действительно задаёт. Сохранение
/// тарифа сюда не добавляется — это другой вопрос другого сценария, и у него
/// будет свой порт. Универсального Repository с двадцатью методами в проекте
/// нет: он заставляет каждый фейк реализовывать девятнадцать лишних.
class TariffRepository {
public:
    TariffRepository(const TariffRepository&) = delete;
    TariffRepository& operator=(const TariffRepository&) = delete;

    virtual ~TariffRepository() = default;

    /// Тариф по его коду; std::nullopt, если такого кода нет.
    virtual std::optional<Tariff> FindByCode(const TariffCode& code) const = 0;

protected:
    TariffRepository() = default;
};

}  // namespace pdr::billing::ports
