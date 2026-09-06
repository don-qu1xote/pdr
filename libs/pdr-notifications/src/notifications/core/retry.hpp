#pragma once

#include <chrono>
#include <optional>

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::notifications {

/// СКОЛЬКО РАЗ ПРОБОВАТЬ И КАК ДОЛГО ЖДАТЬ МЕЖДУ ПОПЫТКАМИ.
///
/// Задержка растёт вдвое с каждой попыткой. Ровный интервал в такой задаче
/// вреден: почтовый узел, который лежит, получает от нас ту же нагрузку каждые
/// тридцать секунд и не встаёт, а сотня строк в очереди превращается в сотню
/// стуков в дверь. Удвоение разносит попытки и даёт узлу время подняться.
///
/// ПРЕДЕЛ ОБЯЗАТЕЛЕН. Строка, которую пробуют вечно, — это не «надёжная
/// доставка», а вечный цикл: адрес умер полгода назад, а мы всё стучимся, и
/// каждая попытка занимает соединение, которого не хватает живым письмам.
/// Исчерпав предел, строка переходит в `gave_up` и остаётся лежать с причиной —
/// удалять её значит потерять единственный след того, что письмо не дошло.
///
/// Часов у политики нет: «сейчас» приходит параметром, а не берётся у системы.
/// Иначе проверить границы предела было бы нечем.
class RetryPolicy final {
public:
    /// Больше суток между попытками не ждём ни при каком числе неудач: письмо,
    /// доставленное через неделю, уже не письмо, а недоумение.
    static constexpr core::Instant::Duration kLongestDelay{std::chrono::hours{24}};

    static core::Result<RetryPolicy> Compose(int max_attempts,
                                             core::Instant::Duration first_delay,
                                             core::Instant::Duration attempt);

    int MaxAttempts() const noexcept {
        return max_attempts_;
    }

    /// До какого момента попытка считается идущей.
    ///
    /// Захват строки двигает её срок вперёд на это время, и пока оно не вышло,
    /// вторая реплика строку не берёт. Воркер, умерший посреди отправки, ничего
    /// не держит: срок истечёт сам, и строка снова станет должной. Это и есть
    /// «не менее одного раза» — с той стороны, где падают.
    core::Instant Deadline(core::Instant now) const noexcept;

    /// Когда пробовать снова после `attempts` неудачных попыток — или «уже
    /// нет», если предел исчерпан.
    ///
    /// `attempts` — сколько попыток УЖЕ сделано, включая только что неудавшуюся.
    /// Первый повтор идёт через `first_delay`, второй через два, третий через
    /// четыре — и так до потолка.
    std::optional<core::Instant> Again(int attempts, core::Instant now) const noexcept;

private:
    RetryPolicy(int max_attempts,
                core::Instant::Duration first_delay,
                core::Instant::Duration attempt) noexcept;

    int max_attempts_;
    core::Instant::Duration first_delay_;
    core::Instant::Duration attempt_;
};

}  // namespace pdr::notifications
