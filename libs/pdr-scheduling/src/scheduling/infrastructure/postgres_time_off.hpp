#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/time_off_repository.hpp"

namespace pdr::scheduling {

/// Перерывы в Postgres.
///
/// НАЛОЖЕНИЕ ПЕРИОДОВ ЛОВИТ БАЗА, А НЕ ЭТОТ КЛАСС. Между чтением «а нет ли уже
/// перерыва на эти дни» и записью помещается второе обращение, и проверка на
/// стороне сценария его не увидит. Ограничение `scheduling_time_off_no_overlap`
/// видит всегда, каким бы ни было чередование, — а сюда его отказ приходит
/// названным значением, потому что «у вас уже есть перерыв на эти дни» это
/// обычный ответ человеку, а не авария.
class PostgresTimeOff final : public ports::TimeOffRepository {
public:
    explicit PostgresTimeOff(infrastructure::db::ScopedTenantContext& scope) noexcept;

    core::Result<void> Save(const TimeOff& period, core::Instant declared_at) override;

    std::optional<TimeOff> Find(const core::TenantId& tenant,
                                const core::TimeOffId& id) const override;

    core::Result<void> Decide(const core::TenantId& tenant,
                              const core::TimeOffId& id,
                              TimeOffDecision lessons,
                              SeriesDecision series,
                              core::Instant at) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
