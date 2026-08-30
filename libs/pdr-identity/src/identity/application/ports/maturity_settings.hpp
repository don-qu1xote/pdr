#pragma once

#include "core/errors.hpp"
#include "identity/core/guardian_access.hpp"

namespace pdr::identity::ports {

/// Правило совершеннолетия из динамического конфига.
///
/// Порт, а не константы: возрастной порог — вопрос права и страны, длина окна
/// на решение — вопрос того, как быстро люди читают почту. Оба меняются без
/// выкатки (`PDR_SELF_ACCOUNT_AGE`, `PDR_GUARDIAN_HANDOVER`).
///
/// Отказ возвращается, а не подменяется умолчанием: правило, которое источник
/// прислал негодным, обязано выглядеть как поломка. Молча взятое умолчание
/// здесь означало бы, что доступ к записям занятий решается числом, о котором
/// никто не просил.
class MaturitySettings {
public:
    MaturitySettings(const MaturitySettings&) = delete;
    MaturitySettings& operator=(const MaturitySettings&) = delete;

    virtual ~MaturitySettings() = default;

    virtual core::Result<MaturityRule> Rule() const = 0;

protected:
    MaturitySettings() = default;
};

}  // namespace pdr::identity::ports
