#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity {

/// Идентификатор строки согласия. Своя метка: строк на одну пару и уровень со
/// временем становится много, и перепутать их с человеком не должно получаться.
using ConsentId = core::StrongId<struct ConsentTag>;

/// НА КАКОМ ОСНОВАНИИ открыт доступ.
///
/// Опека и наблюдение — ОДИН механизм с разными основаниями, и поэтому таблица
/// одна. Несовершеннолетие даёт доступ по умолчанию: родитель обязан видеть,
/// как учится ребёнок, и спрашивать ребёнка об этом никто не будет. Взрослый
/// выдаёт его сам — супругу, тренеру, работодателю, который платит за курс.
/// Заводить под второй случай вторую таблицу значило бы завести и второй набор
/// уровней, второй отзыв и второй журнал, а через полгода они разошлись бы.
///
/// Основание при этом не украшение: от него зависит, ЧТО этот доступ вообще
/// может нести и чем он держится.
enum class ConsentBasis : std::uint8_t {
    /// Несовершеннолетие. Держится действующей опекой: отозвали опеку — доступ
    /// кончился, сколько бы согласий ни осталось строками.
    kGuardianship,

    /// Взрослый назвал наблюдателя сам. Держится только его словом и его же
    /// отзывом; опеки за этим нет и не должно быть.
    kNamedByStudent,

    /// ПЛАТИТ ЗА ЗАНЯТИЯ — И БОЛЬШЕ НИЧЕГО.
    ///
    /// Работодатель оплачивает курс, ученик учится, а конспекты и записи
    /// занятий плательщик не видит. Деньги не дают права смотреть, и здесь это
    /// не пожелание: основание физически не несёт ни одного уровня, кроме
    /// денег (`MayCarry`), и то же ограничение стоит в схеме.
    kPaysForLessons,

    /// ГРАНИЦА СПИСКА, а не основание.
    kBoundary,
};

std::string_view Name(ConsentBasis basis) noexcept;

std::optional<ConsentBasis> ParseConsentBasis(std::string_view text);

inline constexpr std::array<ConsentBasis, 3> kEveryConsentBasis{
    ConsentBasis::kGuardianship,
    ConsentBasis::kNamedByStudent,
    ConsentBasis::kPaysForLessons,
};

static_assert(kEveryConsentBasis.size() == static_cast<std::size_t>(ConsentBasis::kBoundary),
              "основание заведено, а в kEveryConsentBasis его нет");

/// ДЕНЬГИ НЕ ДАЮТ ПРАВА СМОТРЕТЬ.
///
/// Единственное место, где это правило записано, и записано оно так, что обойти
/// его нечем: доступ, выданный за оплату, не выражает ни одного уровня, кроме
/// денег. Не «не выдаём по умолчанию», а «выдать нельзя».
constexpr bool MayCarry(ConsentBasis basis, GuardianScope scope) noexcept {
    return basis != ConsentBasis::kPaysForLessons || scope == GuardianScope::kPayments;
}

/// Основание, которое называет сам ученик, а не обстоятельства.
///
/// Такой доступ не требует опеки — её у взрослого нет вовсе — и держится
/// словом того, о ком он.
constexpr bool NamedByTheStudentHimself(ConsentBasis basis) noexcept {
    return basis == ConsentBasis::kNamedByStudent || basis == ConsentBasis::kPaysForLessons;
}

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
                                               ConsentBasis basis,
                                               core::PersonId granted_by,
                                               core::Instant granted_at,
                                               std::optional<core::Instant> expires_at);

    /// Собрать из хранилища: там строка уже прошла ограничения схемы.
    static GuardianConsent Restore(ConsentId id,
                                   core::TenantId tenant,
                                   core::PersonId guardian,
                                   core::PersonId student,
                                   GuardianScope scope,
                                   ConsentBasis basis,
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
    ConsentBasis Basis() const noexcept {
        return basis_;
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

    /// Держится ли этот доступ действующей опекой. У взрослого опеки нет вовсе,
    /// и его наблюдатель не должен от неё зависеть.
    bool RestsOnGuardianship() const noexcept {
        return basis_ == ConsentBasis::kGuardianship;
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
                    ConsentBasis basis,
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
          basis_{basis},
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
    ConsentBasis basis_;
    core::PersonId granted_by_;
    core::Instant granted_at_;
    std::optional<core::Instant> expires_at_;
    std::optional<core::Instant> revoked_at_;
    std::optional<core::PersonId> revoked_by_;
};

}  // namespace pdr::identity
