#pragma once

#include <optional>

#include "core/types/ids.hpp"
#include "identity/core/birth_date.hpp"

namespace pdr::identity::ports {

/// Узкий порт: когда человек родился.
///
/// Отдельный порт, а не поле в каком-нибудь «профиле»: дата рождения нужна
/// ровно одному правилу — совершеннолетию, — и тащить ради неё целого человека
/// значит завести ещё одно место, где читают чужие контакты.
///
/// Ответа может не быть. У людей, заведённых до того, как дату стали
/// спрашивать, её нет, и правило совершеннолетия для них не срабатывает:
/// отобрать доступ у родителя из-за пустой колонки хуже, чем оставить его до
/// выяснения (docs/runbooks/guardian-access.md).
class BirthDates {
public:
    BirthDates(const BirthDates&) = delete;
    BirthDates& operator=(const BirthDates&) = delete;

    virtual ~BirthDates() = default;

    virtual std::optional<BirthDate> Of(const core::TenantId& tenant,
                                        const core::PersonId& person) const = 0;

protected:
    BirthDates() = default;
};

}  // namespace pdr::identity::ports
