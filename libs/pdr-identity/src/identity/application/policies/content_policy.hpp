#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Права на методические материалы и программы занятий.
///
/// ПРАВИТ МАТЕРИАЛ ТОЛЬКО ЕГО АВТОР. Ни владелец школы, ни другой репетитор:
/// материал — это работа конкретного человека, и правка чужой работы без его
/// ведома ссорит людей надёжнее любой ошибки в расписании.
///
/// Ученик и опекун ВИДЯТ ТО, ЧТО НАЗНАЧЕНО. Библиотеки «всё подряд» здесь нет
/// намеренно: назначенный материал — часть программы, а неназначенный ученику
/// не показывают, потому что показывать его — это уже подбор, и он решается
/// не правами.
class ContentPolicy final : public Policy {
public:
    ContentPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
