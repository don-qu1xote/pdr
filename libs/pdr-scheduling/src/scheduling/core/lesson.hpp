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
#include "scheduling/core/participation.hpp"

namespace pdr::scheduling {

/// Занятие в выставленном репетитором слоте.
///
/// Начало — момент в UTC. Местного времени без зоны здесь нет и быть не может:
/// зона живёт отдельным значением, и превращение одного в другое — это
/// `core::Resolve`, у которого не всегда есть ответ.
///
/// УЧАСТНИКИ — МНОЖЕСТВО УЧАСТИЙ, А НЕ ОДИН УЧЕНИК И НЕ СПИСОК ЛЮДЕЙ.
///
/// Участие несёт то, что у каждого своё: цену, состояние оплаты и исход
/// (`Participation`). Список людей этого не несёт вовсе, и в день групп
/// пришлось бы переписывать хранилище, события и половину сценариев — второе по
/// дороговизне переписывание после тенантности.
///
/// Сегодня участник ровно один, и это ПРАВИЛО, а не форма типа: снять его —
/// правка одной строки в `Schedule`. Отдельной сущности «группа» рядом с
/// занятием нет и не будет: группа — это занятие с несколькими участиями
/// (docs/adr/0023-group-lessons-modelled-not-implemented.md).
class Lesson final {
public:
    using Duration = std::chrono::minutes;

    /// Сколько участников домен допускает сегодня. Правило, а не форма типа:
    /// снятие запрета — правка этого числа, а не модели.
    static constexpr std::size_t kParticipantsForNow = 1;

    /// Назначить занятие. `now` приходит из порта часов — домен «сейчас» не
    /// спрашивает ни у кого.
    ///
    /// Участия, а не люди: кто их собирает, тот и решает, что о них известно.
    /// Записывающий строит `Participation::Joined`, хранилище — то, что лежит в
    /// строке. Второго конструктора «для хранилища» домен не заводит.
    static core::Result<Lesson> Schedule(core::LessonId id,
                                         core::TenantId tenant,
                                         core::PersonId tutor,
                                         std::vector<Participation> participants,
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

    /// УЧАСТНИК ВЫШЕЛ — ЗАНЯТИЕ ОСТАЛОСЬ. Не отмена: состояние занятия не
    /// меняется вовсе, меняется одно участие.
    ///
    /// Последнего участника вывести нельзя: занятие без единого участника — это
    /// отменённое занятие, и называть его выходом значило бы прятать отмену от
    /// того, кто её ищет.
    ///
    /// СЕГОДНЯ ВЫХОД НЕ ПРОХОДИТ НИ РАЗУ: участник всегда один, а значит всегда
    /// последний. Успешная ветка недостижима, пока стоит запрет на второго
    /// участника, — и это не мёртвый код, а тот же запрет, увиденный с другой
    /// стороны (docs/adr/0023-group-lessons-modelled-not-implemented.md).
    core::Result<Change> Withdraw(const CancellationPolicy& policy,
                                  const core::Money& price,
                                  const core::PersonId& participant,
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
    const std::vector<Participation>& Participants() const noexcept {
        return participants_;
    }

    /// Кто занимается — одними идентификаторами. Для тех, кому нужны люди, а не
    /// участия: показ занятия, права, событие о записи.
    std::vector<core::PersonId> People() const;

    /// Участие названного человека. Пусто — он на этом занятии не значится.
    const Participation* Participating(const core::PersonId& person) const noexcept;

    /// ЗАНЯТИЕ С ОБНОВЛЁННЫМ УЧАСТИЕМ. Само занятие при этом не меняется:
    /// цена, оплата и исход — свойства участия, а не занятия.
    ///
    /// Здесь, а не в хранилище: «участие поменялось» — это доменное действие, и
    /// адаптер, собирающий занятие с подменённым участием из кусков, обошёл бы
    /// проверку «а этот человек на занятии вообще есть».
    core::Result<Lesson> With(const Participation& taking) const;
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
    /// Занятие с проставленным каждому участию исходом и ценой.
    Lesson Outcome(Attendance attendance, const core::Money& price) const;

    /// Сколько участников ещё не вышло.
    std::size_t Staying() const noexcept;

    Lesson(core::LessonId id,
           core::TenantId tenant,
           core::PersonId tutor,
           std::vector<Participation> participants,
           core::Instant starts_at,
           Duration duration,
           core::TimeZone zone,
           LessonState state);

    core::LessonId id_;
    core::TenantId tenant_;
    core::PersonId tutor_;
    std::vector<Participation> participants_;
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
