#pragma once

#include <string>

#include "core/types/ids.hpp"
#include "jobs/core/job_name.hpp"

namespace pdr::jobs::ports {

/// След произведённого действия — то, из чего сделана идемпотентность.
///
/// Порт узкий до одного метода намеренно: всё, что нужно механизму, — отличить
/// «этого ещё не делали» от «это уже сделано», и сделать это так, чтобы двое
/// одновременно не получили «ещё не делали» на один ключ.
class JobLedger {
public:
    JobLedger(const JobLedger&) = delete;
    JobLedger& operator=(const JobLedger&) = delete;

    virtual ~JobLedger() = default;

    /// Поставить след действия.
    ///
    /// `true` — след поставлен здесь, действие нужно произвести. `false` — след
    /// уже стоял, действие произведено до нас, и повторять его нельзя.
    ///
    /// Реализация обязана быть атомарной: два воркера, спросившие про один ключ
    /// одновременно, получают `true` и `false`, а не два `true`. В базе это
    /// первичный ключ `jobs_effect`, а не «сначала select, потом insert».
    ///
    /// След ставится ДО действия. Значит, между следом и действием есть окно:
    /// упавший в нём процесс действие потеряет. Выбор сделан осознанно — вторая
    /// рассылка ученику и второе списание дороже несостоявшихся,
    /// docs/adr/0011-single-jobs-on-distlock.md.
    virtual bool Claim(const core::TenantId& tenant,
                       const JobName& job,
                       const std::string& effect_key) = 0;

protected:
    JobLedger() = default;
};

}  // namespace pdr::jobs::ports
