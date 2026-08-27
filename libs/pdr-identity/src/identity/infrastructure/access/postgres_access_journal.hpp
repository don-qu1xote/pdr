#pragma once

#include <vector>

#include "identity/application/ports/access_journal.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Чтение журнала доступа, `identity_access_log`.
///
/// Пишет в ту же таблицу другой адаптер (`PostgresAccessLog`), и это не
/// дублирование: у чтения и записи разные права, разные вызывающие и разные
/// причины меняться. Адаптер, умеющий и то и другое, рано или поздно окажется
/// там, где нужна только одна половина.
///
/// Порядок — от свежего к старому: человек открывает журнал, чтобы увидеть, что
/// было ВЧЕРА, а не в позапрошлом сентябре.
class PostgresAccessJournal final : public ports::AccessJournal {
public:
    explicit PostgresAccessJournal(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::vector<AccessRecord> AboutPerson(const core::TenantId& tenant,
                                          const core::PersonId& subject,
                                          core::Instant since) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
