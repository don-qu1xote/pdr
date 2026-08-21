#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::jobs {

/// Чем закончился прогон. Список закрытый: отсюда берётся и колонка
/// `jobs_run.outcome`, и значение метрики.
enum class Outcome : std::uint8_t {
    kDone,      ///< прогон дошёл до конца плана
    kLockLost,  ///< блокировку отобрали посреди работы — прогон прекращён
    kTimedOut,  ///< отведённое время кончилось раньше плана
};

std::string_view Name(Outcome outcome) noexcept;

/// Запись о прогоне: когда начался, когда кончился, чем кончился и сколько
/// действий произвёл.
///
/// `repeated` — не мусорный счётчик, а главное наблюдаемое свидетельство
/// идемпотентности: он растёт ровно тогда, когда второй воркер подхватил работу
/// и нашёл след первого. Ноль там, где ожидался повтор, значит, что след не
/// сработал и действие произошло дважды.
class RunRecord final {
public:
    static core::Result<RunRecord> Compose(core::Instant started_at,
                                           core::Instant finished_at,
                                           Outcome outcome,
                                           std::int64_t produced,
                                           std::int64_t repeated);

    core::Instant StartedAt() const noexcept {
        return started_at_;
    }
    core::Instant FinishedAt() const noexcept {
        return finished_at_;
    }
    core::Instant::Duration Took() const noexcept {
        return finished_at_ - started_at_;
    }
    Outcome Result() const noexcept {
        return outcome_;
    }
    std::int64_t Produced() const noexcept {
        return produced_;
    }
    std::int64_t Repeated() const noexcept {
        return repeated_;
    }

private:
    RunRecord(core::Instant started_at,
              core::Instant finished_at,
              Outcome outcome,
              std::int64_t produced,
              std::int64_t repeated) noexcept;

    core::Instant started_at_;
    core::Instant finished_at_;
    Outcome outcome_;
    std::int64_t produced_;
    std::int64_t repeated_;
};

/// Сколько задание молчит: от конца последнего прогона до `now`.
core::Instant::Duration SilenceFor(const RunRecord& last, core::Instant now) noexcept;

/// Замолчало ли задание. Не отрабатывавшее ни разу — замолчало: пустой журнал
/// значит, что воркер не поднялся, а не что всё хорошо.
bool HasFallenSilent(const std::optional<RunRecord>& last,
                     core::Instant now,
                     core::Instant::Duration silence_allowed) noexcept;

}  // namespace pdr::jobs
