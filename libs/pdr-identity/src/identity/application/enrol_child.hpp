#pragma once

#include <optional>
#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/email.hpp"

namespace pdr::identity {

/// Кого заводит родитель. ПОЧТЫ ЗДЕСЬ НЕТ, и это главное поле этой структуры —
/// тем, что его нет.
struct EnrolChildRequest final {
    core::TenantId tenant;
    core::PersonId guardian;
    std::string display_name;
    BirthDate born_on;
    core::TimeZone zone;
};

/// Завести ребёнка БЕЗ отдельной учётной записи.
///
/// Семилетнему ученику не нужен почтовый ящик, а требовать его — значит
/// заставить родителя придумать ребёнку почту, которой ребёнок не пользуется,
/// и потом самому же за неё отвечать. Ребёнок появляется как человек практики
/// со своей датой рождения и своей опекой; входить ему пока нечем, и это
/// нормальное состояние, а не наполовину заведённое.
///
/// Учётную запись добавляют потом, когда понадобится, — отдельным действием
/// (`AttachAccount`). Момент, когда она понадобится, определяет возраст, а не
/// мы: с первого порога ученик двигает свои занятия сам (PDR-IDENT-05), и вот
/// тогда ему есть куда входить.
class EnrolChild final {
public:
    EnrolChild(ports::ParticipantDirectory& directory,
               ports::GuardianshipRepository& guardianships,
               const application::ports::IdGenerator& ids,
               const application::ports::Clock& clock) noexcept;

    core::Result<core::PersonId> Execute(const EnrolChildRequest& request) const;

private:
    ports::ParticipantDirectory& directory_;
    ports::GuardianshipRepository& guardianships_;
    const application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
