#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/booking_window_relief.hpp"

namespace pdr::scheduling {

/// Послабления окон в Postgres.
///
/// Строится от ОБЛАСТИ АРЕНДАТОРА, а не от пула: пул в этом заголовке не
/// упоминается вовсе, и «сходить в базу мимо арендатора» здесь нечем написать
/// (`scripts/check_layers.py`).
class PostgresBookingRelief final : public ports::BookingWindowRelief {
public:
    explicit PostgresBookingRelief(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::optional<BookingWindows> For(const core::TenantId& tenant,
                                      const core::PersonId& tutor,
                                      const core::PersonId& student) const override;

    core::Result<void> Grant(const BookingRelief& relief) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
