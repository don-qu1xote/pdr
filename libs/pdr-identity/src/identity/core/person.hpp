#pragma once

#include <optional>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/email.hpp"

namespace pdr::identity {

/// Человек: репетитор, ученик, опекун, самостоятельный взрослый ученик.
///
/// Роли здесь нет — она в `TenantMembership`: один и тот же человек бывает
/// репетитором в своём арендаторе и родителем в чужом, и полем это не
/// выражается. Опекуна здесь тоже нет: связь опеки — отдельное значение, потому
/// что у ученика бывает двое родителей, а у родителя несколько детей.
///
/// Возраста здесь нет по той же причине, по которой его нет в `BirthDate`:
/// человек взрослеет, и число, посчитанное при создании, врёт со следующего дня
/// рождения. Спрашивают у момента: `person.AgeAt(clock.Now())`.
///
/// ПОЧТЫ МОЖЕТ НЕ БЫТЬ, и это не краевой случай. Семилетнего ученика заводит
/// родитель, и требовать для него отдельный почтовый ящик — значит заставить
/// человека придумать ребёнку почту, которой ребёнок не пользуется. Учётная
/// запись добавляется потом, когда понадобится, и это отдельное действие.
///
/// Сеттеров общего назначения нет: значение меняется целиком, новым значением.
class Person final {
public:
    Person(core::PersonId id, std::optional<Email> mail, BirthDate born_on) noexcept
        : id_{std::move(id)}, mail_{std::move(mail)}, born_on_{born_on} {}

    const core::PersonId& Id() const noexcept {
        return id_;
    }
    const std::optional<Email>& Mail() const noexcept {
        return mail_;
    }
    const BirthDate& BornOn() const noexcept {
        return born_on_;
    }

    core::Result<AgeStatus> AgeAt(core::Instant moment) const {
        return AgeStatus::At(born_on_, moment);
    }

    /// Сменить почту — операция предметной области, а не присваивание поля.
    /// Отказа тут не бывает: адрес уже разобран `Email::Parse`, и негодного
    /// значения этого типа не существует.
    Person WithMail(Email mail) const {
        return Person{id_, std::move(mail), born_on_};
    }

    friend bool operator==(const Person&, const Person&) = default;

private:
    core::PersonId id_;
    std::optional<Email> mail_;
    BirthDate born_on_;
};

}  // namespace pdr::identity
