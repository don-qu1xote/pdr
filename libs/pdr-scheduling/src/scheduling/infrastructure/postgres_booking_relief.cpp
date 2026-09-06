#include "scheduling/infrastructure/postgres_booking_relief.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/domain_types.hpp"

namespace pdr::scheduling {
namespace {

std::optional<BookingWindows::Notice> AsNotice(const std::optional<std::int32_t>& minutes) {
    if (!minutes.has_value()) {
        return std::nullopt;
    }
    return BookingWindows::Notice{*minutes};
}

std::optional<std::int32_t> AsMinutes(const std::optional<BookingWindows::Notice>& notice) {
    if (!notice.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(notice->count());
}

}  // namespace

PostgresBookingRelief::PostgresBookingRelief(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<BookingWindows> PostgresBookingRelief::For(const core::TenantId& tenant,
                                                         const core::PersonId& tutor,
                                                         const core::PersonId& student) const {
    const auto rows =
        scope_.Session().Execute(sql::kSchedulingBookingWindowOfPair, tenant, tutor, student);
    if (rows.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        rows.Front().As<SchedulingBookingWindowOfPairRow>(userver::storages::postgres::kRowTag);

    /// Строка, которую мы сами и положили, собирается всегда: отрицательных
    /// значений в неё не пускает схема. Если перестала — чинит это программист,
    /// а не отказ человеку.
    const auto windows = BookingWindows::Compose(AsNotice(row.book_before_minutes),
                                                 AsNotice(row.horizon_minutes),
                                                 AsNotice(row.reschedule_before_minutes),
                                                 AsNotice(row.cancel_before_minutes));
    if (!windows.HasValue()) {
        throw std::runtime_error{"scheduling_booking_window: строка не собирается в окна: " +
                                 windows.Failure().Code()};
    }
    return windows.Value();
}

core::Result<void> PostgresBookingRelief::Grant(const BookingRelief& relief) {
    scope_.Session().Execute(sql::kSchedulingBookingWindowGrant,
                             relief.tenant,
                             relief.tutor,
                             relief.student,
                             AsMinutes(relief.windows.MinNoticeBook()),
                             AsMinutes(relief.windows.MaxHorizonBook()),
                             AsMinutes(relief.windows.MinNoticeReschedule()),
                             AsMinutes(relief.windows.MinNoticeCancel()),
                             relief.granted_by,
                             relief.granted_at);
    return {};
}

}  // namespace pdr::scheduling
