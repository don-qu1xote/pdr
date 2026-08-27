#pragma once

#include <optional>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity {

/// Идентификатор строки согласия. Своя метка: строк на одну пару и уровень со
/// временем становится много, и перепутать их с человеком не должно получаться.
using ConsentId = core::StrongId<struct ConsentTag>;

/// Согласие на один уровень доступа опекуна.
///
/// СТРОКА НА УРОВЕНЬ, а не флаги в одной. Родитель, которому нужно платить, и
/// родитель, которому нужно слушать записи, — это два разных решения; в одной
/// строке они превращаются в «включи, чтобы работало».
///
/// ОТЗЫВ — ЭТО ДАТА, А НЕ УДАЛЕНИЕ. По выданному доступу человек смотрел чужие
/// данные, и на вопрос «кто имел доступ в марте» отвечает журнал, а не память.
/// Удалённая строка отвечает на него «никто», и это неправда.
///
/// `granted_by` не украшение, а то, от чего зависит поведение при
/// совершеннолетии: согласие, которое дал сам ученик, подтверждения больше не
/// требует — он уже сказал своё слово.
class GuardianConsent final {
public:
    static core::Result<GuardianConsent> Grant(ConsentId id,
                                               core::TenantId tenant,
                                               core::PersonId guardian,
                                               core::PersonId student,
                                               GuardianScope scope,
                                               core::PersonId granted_by,
                                               core::Instant granted_at,
                                               std::optional<core::Instant> expires_at);

    /// Собрать из хранилища: там строка уже прошла ограничения схемы.
    static GuardianConsent Restore(ConsentId id,
                                   core::TenantId tenant,
                                   core::PersonId guardian,
                                   core::PersonId student,
                                   GuardianScope scope,
                                   core::PersonId granted_by,
                                   core::Instant granted_at,
                                   std::optional<core::Instant> expires_at,
                                   std::optional<core::Instant> revoked_at,
                                   std::optional<core::PersonId> revoked_by);

    const ConsentId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Guardian() const noexcept {
        return guardian_;
    }
    const core::PersonId& Student() const noexcept {
        return student_;
    }
    GuardianScope Scope() const noexcept {
        return scope_;
    }
    const core::PersonId& GrantedBy() const noexcept {
        return granted_by_;
    }
    core::Instant GrantedAt() const noexcept {
        return granted_at_;
    }
    const std::optional<core::Instant>& ExpiresAt() const noexcept {
        return expires_at_;
    }
    const std::optional<core::Instant>& RevokedAt() const noexcept {
        return revoked_at_;
    }
    const std::optional<core::PersonId>& RevokedBy() const noexcept {
        return revoked_by_;
    }

    /// Сказал ли своё слово сам ученик. Тогда совершеннолетие ничего не меняет.
    bool GrantedByStudent() const noexcept {
        return granted_by_ == student_;
    }

    bool IsActiveAt(core::Instant moment) const noexcept;

    /// Отозвать. Повторный отзыв — ожидаемый отказ, а не авария.
    core::Result<GuardianConsent> Revoked(core::Instant at, core::PersonId by) const;

    friend bool operator==(const GuardianConsent&, const GuardianConsent&) = default;

private:
    GuardianConsent(ConsentId id,
                    core::TenantId tenant,
                    core::PersonId guardian,
                    core::PersonId student,
                    GuardianScope scope,
                    core::PersonId granted_by,
                    core::Instant granted_at,
                    std::optional<core::Instant> expires_at,
                    std::optional<core::Instant> revoked_at,
                    std::optional<core::PersonId> revoked_by) noexcept
        : id_{std::move(id)},
          tenant_{std::move(tenant)},
          guardian_{std::move(guardian)},
          student_{std::move(student)},
          scope_{scope},
          granted_by_{std::move(granted_by)},
          granted_at_{granted_at},
          expires_at_{expires_at},
          revoked_at_{revoked_at},
          revoked_by_{std::move(revoked_by)} {}

    ConsentId id_;
    core::TenantId tenant_;
    core::PersonId guardian_;
    core::PersonId student_;
    GuardianScope scope_;
    core::PersonId granted_by_;
    core::Instant granted_at_;
    std::optional<core::Instant> expires_at_;
    std::optional<core::Instant> revoked_at_;
    std::optional<core::PersonId> revoked_by_;
};

}  // namespace pdr::identity
