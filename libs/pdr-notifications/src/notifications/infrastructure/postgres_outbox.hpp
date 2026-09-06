#pragma once

#include <string>

#include "application/ports/id_generator.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "notifications/application/ports/outbox_repository.hpp"

namespace pdr::notifications {

/// Исходящая очередь в Postgres — СО СТОРОНЫ ТОГО, КТО В НЕЁ КЛАДЁТ.
///
/// Строится от ОБЛАСТИ АРЕНДАТОРА, а не от пула, и это здесь не соблюдение
/// правила ради правила: область — это транзакция обращения, та самая, в
/// которой меняется занятие. Строка очереди ложится в неё же и коммитится
/// вместе с ней. Адаптер, взявший себе соединение, коммитил бы отдельно — и
/// между двумя коммитами помещалось бы падение, ради которого весь
/// транзакционный outbox и написан.
///
/// Разбор очереди сюда не входит: он ходит иначе и лежит отдельно
/// (`PostgresOutboxQueue`).
class PostgresOutbox final : public ports::OutboxRepository {
public:
    PostgresOutbox(infrastructure::db::ScopedTenantContext& scope,
                   const application::ports::IdGenerator& ids) noexcept;

    void Enqueue(const Delivery& delivery, core::Instant due_at) override;

    void Withdraw(const core::TenantId& tenant, const std::string& dedup_key) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::notifications
