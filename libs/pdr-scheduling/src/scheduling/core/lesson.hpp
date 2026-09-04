#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"

namespace pdr::scheduling {

/// Состояние занятия. Список закрытый, и свободного поля у состояния нет:
/// строка в этом месте означает, что через полгода в базе окажется «held »,
/// «HELD» и «проведено», а сравнивать их будет некому.
enum class LessonState : std::uint8_t {
    kPlanned,  ///< назначено, подтверждения ещё нет
    kConfirmed,  ///< подтверждено — обеими сторонами или правилом
    kHeld,       ///< состоялось
    kCancelled,  ///< отменено до начала
    kNoShow,     ///< никто не пришёл

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(LessonState state) noexcept;

inline constexpr std::array<LessonState, 5> kEveryLessonState{
    LessonState::kPlanned,
    LessonState::kConfirmed,
    LessonState::kHeld,
    LessonState::kCancelled,
    LessonState::kNoShow,
};

static_assert(kEveryLessonState.size() == static_cast<std::size_t>(LessonState::kBoundary),
              "состояние заведено, а в kEveryLessonState его нет: обход пропустит его, и "
              "«каждый недопустимый переход отклонён» станет непроверяемым");

/// Что случилось с занятием. Событие — не состояние: «отменить» можно из двух
/// состояний, и приводит оно в одно.
enum class LessonEvent : std::uint8_t {
    kConfirm,
    kHold,
    kCancel,
    kMarkNoShow,

    /// ГРАНИЦА СПИСКА, а не событие.
    kBoundary,
};

std::string_view Name(LessonEvent event) noexcept;

inline constexpr std::array<LessonEvent, 4> kEveryLessonEvent{
    LessonEvent::kConfirm,
    LessonEvent::kHold,
    LessonEvent::kCancel,
    LessonEvent::kMarkNoShow,
};

static_assert(kEveryLessonEvent.size() == static_cast<std::size_t>(LessonEvent::kBoundary),
              "событие заведено, а в kEveryLessonEvent его нет: обход по всем парам "
              "пропустит его, и недопустимый переход останется непроверенным");

/// МАШИНА СОСТОЯНИЙ ЗАНЯТИЯ — ЯВНАЯ И ОДНА.
///
/// Разрешено ровно семь переходов:
///
///     Planned   + Confirm    -> Confirmed
///     Planned   + Hold       -> Held
///     Planned   + Cancel     -> Cancelled
///     Planned   + MarkNoShow -> NoShow
///     Confirmed + Hold       -> Held
///     Confirmed + Cancel     -> Cancelled
///     Confirmed + MarkNoShow -> NoShow
///
/// `Held`, `Cancelled` и `NoShow` — конечные: состоявшееся занятие не
/// отменяется задним числом, отменённое не проводится. Всё, чего нет в списке,
/// возвращает отказ `lesson_transition_not_allowed`, а не бросает исключение:
/// «нажали отмену на уже проведённом» — обычный ответ домена, а не авария.
core::Result<LessonState> Transition(LessonState from, LessonEvent event);

/// Занятие в выставленном репетитором слоте.
///
/// Начало — момент в UTC. Местного времени без зоны здесь нет и быть не может:
/// зона живёт отдельным значением, и превращение одного в другое — это
/// `core::Resolve`, у которого не всегда есть ответ.
///
/// УЧАСТНИКИ — ВЕКТОР, А НЕ ОДИН УЧЕНИК. Групповые занятия появятся
/// (PDR-SCHED-08), и переделывать под них тип — это переписывать хранилище,
/// события и половину сценариев. Сегодняшнее доменное правило ограничивает
/// размер единицей, и это ПРАВИЛО, которое можно ослабить одной строкой, а не
/// форма типа, которую придётся менять везде.
class Lesson final {
public:
    using Duration = std::chrono::minutes;

    /// Сколько участников домен допускает сегодня. Правило, а не форма типа.
    static constexpr std::size_t kParticipantsForNow = 1;

    /// Назначить занятие. `now` приходит из порта часов — домен «сейчас» не
    /// спрашивает ни у кого.
    static core::Result<Lesson> Schedule(core::LessonId id,
                                         core::TenantId tenant,
                                         core::PersonId tutor,
                                         std::vector<core::PersonId> participants,
                                         core::Instant starts_at,
                                         Duration duration,
                                         core::Instant now);

    /// Занятие после события. Возвращается НОВОЕ значение: занятие — величина, а
    /// не изменяемый объект, и «состояние поменялось у копии» здесь невыразимо.
    core::Result<Lesson> After(LessonEvent event) const;

    const core::LessonId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Tutor() const noexcept {
        return tutor_;
    }
    const std::vector<core::PersonId>& Participants() const noexcept {
        return participants_;
    }
    core::Instant StartsAt() const noexcept {
        return starts_at_;
    }
    Duration LessonDuration() const noexcept {
        return duration_;
    }
    LessonState State() const noexcept {
        return state_;
    }

    core::Instant EndsAt() const noexcept {
        return starts_at_ + duration_;
    }

    /// Занятие как отрезок времени — то, чем оно и является для пересечений и
    /// для доступности.
    core::TimeRange Span() const;

private:
    Lesson(core::LessonId id,
           core::TenantId tenant,
           core::PersonId tutor,
           std::vector<core::PersonId> participants,
           core::Instant starts_at,
           Duration duration,
           LessonState state);

    core::LessonId id_;
    core::TenantId tenant_;
    core::PersonId tutor_;
    std::vector<core::PersonId> participants_;
    core::Instant starts_at_;
    Duration duration_;
    LessonState state_;
};

}  // namespace pdr::scheduling
