#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"

namespace pdr::events::identity {

/// Ученик стал взрослым: начался срок на решение о родительском доступе.
///
/// ЭТО НЕ ОТКЛЮЧЕНИЕ, а предупреждение обеим сторонам. Мгновенный обрыв ломает
/// семьи в середине учебного года: родитель перестаёт видеть занятия, за
/// которые платит, и виноватой оказывается платформа. До `decide_by` всё
/// работает по-прежнему; после — чувствительные уровни закрываются, пока
/// ученик не откроет их сам.
///
/// Издатель — контекст identity. Подписчик заводится в своём модуле и не
/// требует ни строчки правки здесь и у издателя.
struct GuardianHandoverStarted final {
    static constexpr std::string_view kType = "identity.guardian_handover_started";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::PersonId guardian;
    core::PersonId student;
    core::Instant decide_by;
};

}  // namespace pdr::events::identity
