#pragma once

#include "identity/application/ports/configuration_faults.hpp"

namespace pdr::identity {

/// Поломки настройки — в журнал, уровнем ошибки.
///
/// Уровнем ошибки, а не предупреждения: незаведённая политика означает, что
/// новое действие тихо запрещено всем, и выясняется это по жалобам через
/// неделю после выкатки. На такое смотрят сразу или не смотрят никогда.
///
/// Ни арендатора, ни человека в записи нет: имя действия отвечает на вопрос
/// «что чинить» целиком, а всё остальное только сделало бы журнал местом, где
/// накапливаются чужие идентификаторы.
class LoggingConfigurationFaults final : public ports::ConfigurationFaults {
public:
    LoggingConfigurationFaults() = default;

    void NoPolicyFor(Action action) const override;
};

}  // namespace pdr::identity
