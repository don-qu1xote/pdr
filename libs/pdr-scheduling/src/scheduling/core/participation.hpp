#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/money.hpp"
#include "core/types/ids.hpp"

namespace pdr::scheduling {

/// Оплачено ли ЭТО участие. Не занятие — участие.
///
/// В группе один заплатил, второй нет, а занятие одно: состояние оплаты у
/// занятия целиком не выражается вовсе. Сегодня участник ровно один, и разницы
/// не видно — но модель обязана уметь то, чего сегодня не бывает, иначе в день
/// групп переписывать придётся хранилище, события и половину сценариев.
///
/// Что случилось с деньгами дальше — возврат, удержание, зачёт из пакета —
/// вопрос биллинга, и расписание про это не знает ничего. Здесь ровно два
/// ответа: заплачено за это участие или ещё нет.
enum class PaymentState : std::uint8_t {
    kUnpaid,
    kPaid,

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(PaymentState state) noexcept;

inline constexpr std::array<PaymentState, 2> kEveryPaymentState{
    PaymentState::kUnpaid,
    PaymentState::kPaid,
};

static_assert(kEveryPaymentState.size() == static_cast<std::size_t>(PaymentState::kBoundary),
              "состояние оплаты заведено, а в kEveryPaymentState его нет: обход пропустит "
              "его, и «у каждого состояния есть значение в схеме» станет непроверяемым");

/// БЫЛ ЛИ ЧЕЛОВЕК НА ЗАНЯТИИ. Своё у каждого участника.
///
/// Занятие общее, а освоение у каждого своё: в группе из троих один не пришёл,
/// и занятие при этом состоялось. Прогресс читает именно этот факт, а не
/// состояние занятия, — и читает его по участнику.
enum class Attendance : std::uint8_t {
    /// Ещё не наступило или ещё не отмечено.
    kExpected,

    kAttended,
    kMissed,

    /// ГРАНИЦА СПИСКА, а не исход.
    kBoundary,
};

std::string_view Name(Attendance attendance) noexcept;

inline constexpr std::array<Attendance, 3> kEveryAttendance{
    Attendance::kExpected,
    Attendance::kAttended,
    Attendance::kMissed,
};

static_assert(kEveryAttendance.size() == static_cast<std::size_t>(Attendance::kBoundary),
              "исход заведён, а в kEveryAttendance его нет: обход пропустит его, и «у "
              "каждого исхода есть значение в схеме» станет непроверяемым");

/// УЧАСТНИК ВЫШЕЛ ≠ ЗАНЯТИЕ ОТМЕНЕНО. Два разных факта и два разных следствия.
///
/// В группе выход одного не отменяет занятия для остальных: оно состоится, и
/// платят за него те, кто остался. Отмена занятия касается всех сразу.
/// Состояние занятия и состояние участия поэтому разные величины, и одно из
/// другого не выводится.
enum class ParticipationState : std::uint8_t {
    kJoined,
    kWithdrawn,

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(ParticipationState state) noexcept;

inline constexpr std::array<ParticipationState, 2> kEveryParticipationState{
    ParticipationState::kJoined,
    ParticipationState::kWithdrawn,
};

static_assert(kEveryParticipationState.size() ==
                  static_cast<std::size_t>(ParticipationState::kBoundary),
              "состояние участия заведено, а в kEveryParticipationState его нет: обход пропустит "
              "его, и «у каждого состояния есть значение в схеме» станет непроверяемым");

/// УЧАСТИЕ: чьё оно, почём, оплачено ли и чем кончилось.
///
/// Не идентификатор человека, и это вся задача PDR-SCHED-08. Занятие с
/// колонкой `participant_id` пришлось бы переписывать целиком в тот день, когда
/// участников станет двое; занятие со списком участий не придётся — поменяется
/// одно доменное правило.
///
/// ЦЕНА ЗДЕСЬ, А НЕ У ЗАНЯТИЯ, и пустой она бывает не от небрежности. Цену
/// назначает не расписание: у репетитора свой тариф, у связки с учеником может
/// быть своя цена, и приходит она сюда извне. Пустая цена значит «ещё не
/// назначена», а не «бесплатно»: бесплатное участие — это назначенный ноль.
///
/// Величина — значение, а не изменяемый объект: каждый переход возвращает новое
/// участие. «Состояние поменялось у копии» здесь невыразимо.
class Participation final {
public:
    /// Человек записан. Единственный способ завести участие: цена, оплата и
    /// исход появляются потом и по одному, каждый своим переходом.
    static Participation Joined(core::PersonId person);

    const core::PersonId& Person() const noexcept {
        return person_;
    }

    /// Цена ЭТОГО участия, если она уже назначена.
    const std::optional<core::Money>& Price() const noexcept {
        return price_;
    }

    PaymentState Payment() const noexcept {
        return payment_;
    }
    Attendance Attended() const noexcept {
        return attendance_;
    }
    ParticipationState State() const noexcept {
        return state_;
    }

    Participation Priced(core::Money price) const;
    Participation Paid() const;
    Participation Came() const;
    Participation Missed() const;
    Participation Withdrawn() const;

    friend bool operator==(const Participation&, const Participation&) = default;

private:
    Participation(core::PersonId person,
                  std::optional<core::Money> price,
                  PaymentState payment,
                  Attendance attendance,
                  ParticipationState state) noexcept
        : person_{std::move(person)},
          price_{std::move(price)},
          payment_{payment},
          attendance_{attendance},
          state_{state} {}

    core::PersonId person_;
    std::optional<core::Money> price_;
    PaymentState payment_{PaymentState::kUnpaid};
    Attendance attendance_{Attendance::kExpected};
    ParticipationState state_{ParticipationState::kJoined};
};

}  // namespace pdr::scheduling
