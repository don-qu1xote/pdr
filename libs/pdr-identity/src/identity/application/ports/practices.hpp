#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/practice.hpp"
#include "identity/core/tenant.hpp"

namespace pdr::identity::ports {

/// Практики и их видимость снаружи.
///
/// ОЧЕРЕДИ НА РАЗБОР ЗДЕСЬ НЕТ, и это не пропуск. Она общая по определению —
/// разбирает её не репетитор, а мы, — а `identity_tenant` несёт арендатора, и
/// читать её мимо построчной защиты нельзя ничем: вторая дверь
/// (`infrastructure::db::UnscopedAccess`) закрыта для таблиц с `tenant_id`
/// именно затем, чтобы такого соблазна не возникало.
///
/// Поэтому очередь — работа оператора и отдельный запрос под ролью миграций
/// (`db/practice/queue.sql`, `make practice-queue`), а сценарий разбирает по
/// одной названной практике, изнутри её области.
class Practices {
public:
    Practices(const Practices&) = delete;
    Practices& operator=(const Practices&) = delete;

    virtual ~Practices() = default;

    /// Завести практику: строка арендатора и её видимость появляются вместе.
    ///
    /// Вместе, а не двумя вызовами: практика без состояния видимости — это
    /// практика, про которую нельзя ответить, видна она или нет, и отвечать на
    /// такое умолчанием значит однажды ответить «да».
    virtual core::Result<void> Open(const Tenant& tenant,
                                    const core::TimeZone& zone,
                                    const Practice& practice) = 0;

    virtual std::optional<Practice> Find(const core::TenantId& tenant) const = 0;

    virtual void Save(const Practice& practice) = 0;

protected:
    Practices() = default;
};

}  // namespace pdr::identity::ports
