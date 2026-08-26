#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "core/types/ids.hpp"
#include "identity/contract.hpp"
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
    Subject(core::TenantId tenant, core::PersonId person, RoleSet roles, Tie tie) noexcept
        : tenant_{std::move(tenant)}, person_{std::move(person)}, roles_{roles}, tie_{tie} {}

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

private:
    core::TenantId tenant_;
    core::PersonId person_;
    RoleSet roles_;
    Tie tie_;
};

/// Кем человек приходится ресурсу. ЕДИНСТВЕННОЕ МЕСТО, где это считается.
///
/// Опека приходит готовым ответом, а не запросом отсюда: считать её умеет
/// только тот, у кого есть хранилище, и это `ContractService`.
Tie TieBetween(const core::PersonId& person,
               const Resource& resource,
               bool guards_subject) noexcept;

}  // namespace pdr::identity
