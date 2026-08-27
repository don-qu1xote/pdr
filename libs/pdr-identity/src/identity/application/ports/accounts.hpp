#pragma once

#include <optional>

#include "identity/core/account.hpp"
#include "identity/core/digest.hpp"

namespace pdr::identity::ports {

/// Реестр учётных записей — ЕДИНСТВЕННОЕ, что пересекает границу арендатора.
///
/// Пересекает по необходимости: вопрос «этот человек уже есть на площадке?»
/// без такого пересечения не задать вовсе, а без ответа на него ученик,
/// которого позвал второй репетитор, оказывается вторым человеком.
///
/// Порт узкий сознательно. Найти по отпечатку почты и сохранить — всё. Ни
/// перечисления записей, ни поиска по частям адреса, ни «сколько у него
/// репетиторов»: любой из этих вопросов означал бы, что через границу
/// арендатора можно узнать что-то о чужой практике.
class Accounts {
public:
    Accounts(const Accounts&) = delete;
    Accounts& operator=(const Accounts&) = delete;

    virtual ~Accounts() = default;

    virtual std::optional<Account> FindByMail(const Digest& mail) const = 0;

    virtual std::optional<Account> FindById(const core::PersonId& id) const = 0;

    virtual void Save(const Account& account) = 0;

protected:
    Accounts() = default;
};

}  // namespace pdr::identity::ports
