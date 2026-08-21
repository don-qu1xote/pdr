#pragma once

#include "core/errors.hpp"
#include "core/types/time.hpp"
#include "jobs/core/job_name.hpp"

namespace pdr::jobs {

/// Настройки одного периодического задания.
///
/// Ни одного числа из этих четырёх в коде нет: имя блокировки, период, отведённое
/// на прогон время и срок молчания приходят из динамического конфига
/// (`PDR-CFG-01`), а сюда — уже значениями. Константа означала бы, что смена
/// периода рассылки стоит выкатки, а значит, период поменяют «временно и руками».
///
/// Проверки здесь доменные, и каждая про свою беду:
///
/// * прогон, которому отведено больше, чем период, наезжает на следующий: пока
///   первый доделывает, второй уже пора начинать;
/// * задание, объявленное замолчавшим раньше, чем оно обязано было проснуться,
///   даёт ложную тревогу на каждом периоде — и через неделю на эту метрику
///   перестают смотреть.
class JobSettings final {
public:
    using Duration = core::Instant::Duration;

    static core::Result<JobSettings> Compose(
        JobName lock, Duration period, Duration attempt, Duration silence_allowed, bool enabled);

    /// Имя строки блокировки: под ним задание берёт `jobs_lock`.
    const JobName& Lock() const noexcept {
        return lock_;
    }

    /// Как часто задание просыпается.
    Duration Period() const noexcept {
        return period_;
    }

    /// Сколько отведено одному прогону. Дольше — прогон прекращается сам, не
    /// дожидаясь, пока блокировку отберут.
    Duration Attempt() const noexcept {
        return attempt_;
    }

    /// После какого молчания задание считается замолчавшим. Для суточных
    /// сверок это сутки, для рассылки напоминаний — минуты.
    Duration SilenceAllowed() const noexcept {
        return silence_allowed_;
    }

    /// Выключенное задание блокировку не берёт вовсе. Это единственный
    /// предусмотренный способ остановить его без выкатки.
    bool Enabled() const noexcept {
        return enabled_;
    }

private:
    JobSettings(JobName lock,
                Duration period,
                Duration attempt,
                Duration silence_allowed,
                bool enabled) noexcept;

    JobName lock_;
    Duration period_;
    Duration attempt_;
    Duration silence_allowed_;
    bool enabled_;
};

}  // namespace pdr::jobs
