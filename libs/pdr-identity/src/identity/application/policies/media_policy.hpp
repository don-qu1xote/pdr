#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Права на содержание занятия: запись и транскрипт.
///
/// САМЫЙ ЧУВСТВИТЕЛЬНЫЙ УРОВЕНЬ. Запись урока — это час голоса ребёнка, его
/// ошибок и его разговора с чужим взрослым. Поэтому опекуну она не открывается
/// вместе с опекой: нужно отдельное согласие, а после совершеннолетия — слово
/// самого ученика ([ADR-0006](../../../../../docs/adr/0006-parental-access-by-design.md)).
///
/// Владельца школы здесь нет ни при каком уровне. Он ведёт школу, а не слушает
/// занятия: «владелец может всё» стоило бы ровно этих записей.
class MediaPolicy final : public Policy {
public:
    MediaPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
