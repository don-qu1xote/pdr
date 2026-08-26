#pragma once

#include <string>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/person.hpp"

namespace pdr::identity::ports {

/// Всё, что нужно, чтобы человек появился: он сам, его роль и то, чего в
/// доменном `Person` нет.
///
/// Имени для показа и зоны в `Person` нет намеренно — их не завела PDR-IDENT-01,
/// и додумывать их в домене задним числом здесь незачем. Схема их требует
/// (`identity_person.display_name`, `identity_person.tz`), поэтому они едут
/// рядом, и видно, что это данные строки, а не правила предметной области.
struct Enrolment final {
    Person person;
    Role role;
    std::string display_name;
    core::TimeZone zone;
};

/// Заведение участника: человек и его роль появляются вместе.
///
/// Вместе, а не двумя вызовами, потому что человек без роли ни на что не
/// годен, а роль без человека не выражается вовсе. Разделив их, мы бы получили
/// состояние «наполовину заведён» ровно в тот момент, когда сеть моргнула.
class ParticipantDirectory {
public:
    ParticipantDirectory(const ParticipantDirectory&) = delete;
    ParticipantDirectory& operator=(const ParticipantDirectory&) = delete;

    virtual ~ParticipantDirectory() = default;

    /// Отказ здесь ожидаемый, а не аварийный: почта уже занята в этом
    /// арендаторе. Проверять её заранее отдельным вопросом незачем — ответ на
    /// такой вопрос сообщает постороннему, кто у нас учится.
    virtual core::Result<void> Enrol(const core::TenantId& tenant, const Enrolment& enrolment) = 0;

protected:
    ParticipantDirectory() = default;
};

}  // namespace pdr::identity::ports
