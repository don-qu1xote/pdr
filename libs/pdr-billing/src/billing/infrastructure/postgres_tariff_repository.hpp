#pragma once

#include <optional>

#include "billing/application/ports/tariff_repository.hpp"
#include "billing/core/tariff.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::billing {

/// Адаптер порта поверх Postgres — обычный класс с обычным конструктором.
///
/// Он НЕ наследует components::ComponentBase, и это главное решение этого файла.
/// Репозиторий, сросшийся с компонентом userver, невозможно создать без
/// поднятого сервиса: тогда любой тест сценария превращается в интеграционный,
/// с реальным бинарником и реальной базой. Компонент — это способ создать
/// адаптер, а не сам адаптер; он лежит рядом и умеет ровно это.
///
/// Строится ОТ ОБЛАСТИ АРЕНДАТОРА, а не от пула соединений: тарифы — доменные
/// данные, и запрос к ним без объявленного арендатора не должен быть выразим.
/// Пул в этом заголовке не упоминается вовсе, и это проверяется машиной
/// (`scripts/check_layers.py`).
class PostgresTariffRepository final : public ports::TariffRepository {
public:
    explicit PostgresTariffRepository(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::optional<Tariff> FindByCode(const TariffCode& code) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::billing
