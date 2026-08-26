#pragma once

#include "identity/contract.hpp"

namespace pdr::identity::ports {

/// Куда сообщают о поломках настройки.
///
/// Порт, а не журнал напрямую: журнал живёт в userver, а слой сценариев про
/// него не знает и знать не будет (`scripts/check_layers.py`).
///
/// Метод один, и это не заготовка на будущее. Незаведённая политика — не отказ
/// человеку, а ошибка того, кто завёл действие и не связал его с правилом;
/// такое обязано выглядеть как поломка, иначе новое действие тихо запрещается
/// всем и выясняется это по жалобам.
class ConfigurationFaults {
public:
    ConfigurationFaults(const ConfigurationFaults&) = delete;
    ConfigurationFaults& operator=(const ConfigurationFaults&) = delete;

    virtual ~ConfigurationFaults() = default;

    virtual void NoPolicyFor(Action action) const = 0;

protected:
    ConfigurationFaults() = default;
};

}  // namespace pdr::identity::ports
