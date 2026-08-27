#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "core/types/ids.hpp"
#include "identity/contract.hpp"
#include "identity/core/capabilities.hpp"
#include "identity/core/guardian_access.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity {

/// Кем спрашивающий приходится ресурсу.
///
/// ОТНОШЕНИЕ СЧИТАЕТСЯ ДО ПОЛИТИКИ, а не внутри неё. «Опекун ли он» — вопрос к
/// хранилищу, и политика, которая его задаёт, перестаёт быть чистой функцией:
/// её больше не проверить таблицей, только поднятым сервисом с базой.
///
/// Значения упорядочены от близкого к далёкому, и порядок содержательный:
/// у занятия есть и ведущий репетитор, и ученик, поэтому одному человеку могут
/// подходить сразу два. Берётся ближайшее — то, которым он приходится ресурсу
/// в первую очередь.
enum class Tie : std::uint8_t {
    /// Он ведёт этот ресурс: репетитор занятия, выставивший счёт, автор материала.
    kMine,
    /// Ресурс о нём самом.
    kAboutMe,
    /// Ресурс о том, кого он опекает.
    kMyWard,
    /// Никем: ресурс кабинета вообще или чужой.
    kNone,
};

std::string_view Name(Tie tie) noexcept;

/// Кто спрашивает — вместе с ролями и отношением к ЭТОМУ ресурсу.
///
/// Собирается на каждый вопрос заново, поэтому отношение здесь не противоречит
/// названию: субъект существует ровно на время одного решения.
///
/// Ролей может не быть вовсе — человек из этого кабинета, но ни в какой роли.
/// Это не краевой случай, а обычное состояние приглашённого, который ещё не
/// пришёл по ссылке.
class Subject final {
public:
    Subject(core::TenantId tenant,
            core::PersonId person,
            RoleSet roles,
            Tie tie,
            GuardianAccess access,
            Capabilities able) noexcept
        : tenant_{std::move(tenant)},
          person_{std::move(person)},
          roles_{roles},
          tie_{tie},
          access_{access},
          able_{able} {}

    /// Без собственных возможностей — то есть с возможностями младенца.
    Subject(core::TenantId tenant,
            core::PersonId person,
            RoleSet roles,
            Tie tie,
            GuardianAccess access) noexcept
        : Subject{std::move(tenant), std::move(person), roles, tie, access, Capabilities{}} {}

    /// Без опекунских уровней и без своих возможностей — то есть без единого.
    ///
    /// Умолчание здесь ЗАПРЕТ, а не «всё открыто»: субъект, собранный
    /// невнимательно, обязан получать отказ, а не чужую запись занятия.
    Subject(core::TenantId tenant, core::PersonId person, RoleSet roles, Tie tie) noexcept
        : Subject{std::move(tenant),
                  std::move(person),
                  roles,
                  tie,
                  GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}}} {}

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Person() const noexcept {
        return person_;
    }
    const RoleSet& Roles() const noexcept {
        return roles_;
    }
    Tie TiedAs() const noexcept {
        return tie_;
    }

    /// Что ему открыто как опекуну. Считается до политики — из согласий и
    /// возраста подопечного (`WeighConsents`), потому что и то и другое живёт
    /// в хранилище.
    const GuardianAccess& AsGuardian() const noexcept {
        return access_;
    }

    /// Что он может САМ — по возрасту, а не по выданному праву.
    ///
    /// Считается до политики из даты рождения и порогов конфига
    /// (`identity::Compute`). Хранимого поля за этим нет и быть не может: оно
    /// устарело бы в полночь дня рождения, а права приходят сами.
    const Capabilities& Able() const noexcept {
        return able_;
    }

private:
    core::TenantId tenant_;
    core::PersonId person_;
    RoleSet roles_;
    Tie tie_;
    GuardianAccess access_;
    Capabilities able_;
};

/// Кем человек приходится ресурсу. ЕДИНСТВЕННОЕ МЕСТО, где это считается.
///
/// Опека приходит готовым ответом, а не запросом отсюда: считать её умеет
/// только тот, у кого есть хранилище, и это `ContractService`.
Tie TieBetween(const core::PersonId& person,
               const Resource& resource,
               bool guards_subject) noexcept;

}  // namespace pdr::identity
