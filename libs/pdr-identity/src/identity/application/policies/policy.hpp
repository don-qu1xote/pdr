#pragma once

#include "identity/application/policies/subject.hpp"
#include "identity/contract.hpp"

namespace pdr::identity::policies {

/// Правило доступа: можно ли этому человеку это действие над этим ресурсом.
///
/// ЧИСТАЯ ФУНКЦИЯ. Ни хранилища, ни часов, ни сети: всё, что нужно решению,
/// уже лежит в `Subject` и `Resource`. Поэтому политику проверяют таблицей —
/// все роли на все действия за миллисекунды, — а не поднятым сервисом.
///
/// Наружу этот интерфейс не выходит: чужие контексты видят `identity::Contract`
/// и спрашивают его. Хендлер, который умеет собрать `Subject` сам, — это
/// хендлер, который решает права сам, и через год их будет двадцать разных.
/// Копирование здесь РАЗРЕШЕНО производным типам и запрещено через ссылку на
/// базу — тем, что оно защищённое. Остальные интерфейсы проекта копирование
/// удаляют; здесь оно нужно: политики собираются из мелких правил значениями
/// (`AllOf{HasRole{...}, Tied{...}}`), а тип, который нельзя ни скопировать, ни
/// переместить, в такое выражение не поставить. Срезка при этом по-прежнему не
/// выражается: `Policy` — абстрактный, и копию по ссылке на базу не сделать.
class Policy {
public:
    virtual ~Policy() = default;

    virtual PolicyDecision Decide(const Subject& subject,
                                  Action action,
                                  const Resource& resource) const = 0;

protected:
    Policy() = default;
    Policy(const Policy&) = default;
    Policy(Policy&&) = default;
    Policy& operator=(const Policy&) = default;
    Policy& operator=(Policy&&) = default;
};

}  // namespace pdr::identity::policies
