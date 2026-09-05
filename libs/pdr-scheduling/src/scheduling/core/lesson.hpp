#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/money.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/cancellation_policy.hpp"
#include "scheduling/core/lesson_history.hpp"
#include "scheduling/core/lesson_state.hpp"

namespace pdr::scheduling {

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
                                         core::TimeZone zone,
                                         core::Instant now);

    /// Занятие после события. Возвращается НОВОЕ значение: занятие — величина, а
    /// не изменяемый объект, и «состояние поменялось у копии» здесь невыразимо.
    core::Result<Lesson> After(LessonEvent event) const;

    /// Занятие после операции вместе с расчётом и записью в историю.
    ///
    /// Три вещи разом, потому что порознь они расходятся: состояние поменялось,
    /// а удержание посчитали по старому; удержание посчитали, а в историю не
    /// записали. Операция отдаёт всё, что она произвела, и вызывающему остаётся
    /// сохранить это.
    struct Change;

    /// ОТМЕНА УЧЕНИКОМ. Внутри окна — бесплатно, позже — доля из политики.
    core::Result<Change> CancelByStudent(const CancellationPolicy& policy,
                                         const core::Money& price,
                                         const core::PersonId& actor,
                                         core::Instant now) const;

    /// ОТМЕНА РЕПЕТИТОРОМ — ВСЕГДА БЕЗ УДЕРЖАНИЯ, КАКОЙ БЫ НИ БЫЛА ПОЛИТИКА.
    ///
    /// Политики в списке доводов поэтому нет вовсе: «независимо от политики» —
    /// не оговорка в комментарии, а отсутствующий параметр, и обойти его нечем.
    /// Валюта нужна затем, что нуль тоже в чём-то измеряется; цена — нет, её
    /// здесь не на что умножать.
    core::Result<Change> CancelByTutor(const core::CurrencyCode& currency,
                                       const core::PersonId& actor,
                                       core::Instant now) const;

    /// ПЕРЕНОС — НЕ «ОТМЕНА ПЛЮС СОЗДАНИЕ». Возвращается ТО ЖЕ занятие: тот же
    /// `LessonId`, та же оплата, тот же прогресс. Новое занятие на этом месте
    /// разорвало бы связь и с тем и с другим, и восстановить её было бы нечем.
    ///
    /// Сколько переносов уже было, считается по ИСТОРИИ, а не приходит числом:
    /// число можно передать не то, а история — то самое место, где перенос и
    /// записан.
    core::Result<Change> Reschedule(const CancellationPolicy& policy,
                                    const core::Money& price,
                                    const core::PersonId& actor,
                                    core::Instant to,
                                    core::Instant now,
                                    std::span<const LessonHistoryEntry> history) const;

    /// Занятие состоялось. Удержано всё — и это не удержание, а плата.
    core::Result<Change> MarkHeld(const core::Money& price,
                                  const core::PersonId& actor,
                                  core::Instant now) const;

    /// Никто не пришёл. Доля своя, отдельная от поздней отмены: не пришедший и
    /// отменивший за час — разные поступки.
    core::Result<Change> MarkNoShow(const CancellationPolicy& policy,
                                    const core::Money& price,
                                    const core::PersonId& actor,
                                    core::Instant now) const;

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

    /// ЗОНА, В КОТОРОЙ ЗАНЯТИЕ ЗАДУМАНО, А НЕ ЗОНА ПОКАЗА.
    ///
    /// Момент отвечает на «когда», зона — на «во сколько это было по часам
    /// того, кто назначал». Занятие в 18:00 по Берлину, перенесённое
    /// государством на час, обязано остаться в 18:00 по Берлину, и без зоны
    /// рядом с моментом это невыразимо (PDR-DB-01, docs/architecture/migrations.md).
    const core::TimeZone& Zone() const noexcept {
        return zone_;
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
           core::TimeZone zone,
           LessonState state);

    core::LessonId id_;
    core::TenantId tenant_;
    core::PersonId tutor_;
    std::vector<core::PersonId> participants_;
    core::Instant starts_at_;
    Duration duration_;
    core::TimeZone zone_;
    LessonState state_;
};

struct Lesson::Change final {
    /// То же занятие в новом виде. `Id()` у него ТОТ ЖЕ — и у переноса тоже.
    Lesson lesson;
    CancellationOutcome outcome;
    LessonHistoryEntry record;
};

}  // namespace pdr::scheduling
