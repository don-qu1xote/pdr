#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "scheduling/application/ports/availability_repository.hpp"
#include "scheduling/application/ports/lesson_history.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/application/ports/local_days.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"
#include "scheduling/application/ports/time_off_repository.hpp"
#include "scheduling/core/overlap.hpp"
#include "scheduling/core/participation.hpp"
#include "scheduling/core/time_off.hpp"

namespace pdr::scheduling::testing {

/// Занятия в памяти.
///
/// ОТКАЗЫВАЕТ ТАМ ЖЕ, ГДЕ ОТКАЖЕТ БАЗА. Пересечение у репетитора запрещено
/// ограничением `scheduling_lesson_no_overlap`; фейк, принимающий такое занятие
/// молча, делает unit-прогон зелёным на поведении, которого в проде нет. Что
/// обе реализации отказывают одинаково, проверяет contract-набор.
class FakeLessons final : public ports::LessonRepository {
public:
    std::optional<Lesson> Find(const core::TenantId& tenant,
                               const core::LessonId& id) const override {
        for (const auto& lesson : kept_) {
            if (lesson.Tenant() == tenant && lesson.Id() == id) {
                return lesson;
            }
        }
        return std::nullopt;
    }

    std::optional<Lesson> FindAtSlot(const core::TenantId& tenant,
                                     const core::PersonId& tutor,
                                     core::Instant starts_at) const override {
        for (const auto& lesson : kept_) {
            if (lesson.Tenant() == tenant && lesson.Tutor() == tutor &&
                lesson.StartsAt() == starts_at) {
                return lesson;
            }
        }
        return std::nullopt;
    }

    std::vector<Lesson> OfTutor(const core::TenantId& tenant,
                                const core::PersonId& tutor,
                                const core::TimeRange& window) const override {
        return Within(window, [&](const Lesson& lesson) {
            return lesson.Tenant() == tenant && lesson.Tutor() == tutor;
        });
    }

    std::vector<Lesson> OfParticipant(const core::TenantId& tenant,
                                      const core::PersonId& participant,
                                      const core::TimeRange& window) const override {
        return Within(window, [&](const Lesson& lesson) {
            return lesson.Tenant() == tenant && lesson.Participating(participant) != nullptr;
        });
    }

    core::Result<void> SetState(const Lesson& lesson) override {
        return Replace(lesson);
    }

    core::Result<void> Move(const Lesson& lesson) override {
        for (const auto& other : kept_) {
            if (other.Id() != lesson.Id() && other.Tenant() == lesson.Tenant() &&
                other.Tutor() == lesson.Tutor() && Busy(other) && Busy(lesson) &&
                Overlaps(other, lesson, core::Instant::Duration::zero())) {
                return core::Error{core::ErrorKind::kConflict,
                                   "slot_already_taken",
                                   "это время у репетитора уже занято"};
            }
        }
        return Replace(lesson);
    }

    core::Result<void> SetParticipation(const core::TenantId& tenant,
                                        const core::LessonId& lesson,
                                        const Participation& taking) override {
        for (auto& kept : kept_) {
            if (kept.Tenant() != tenant || kept.Id() != lesson) {
                continue;
            }
            auto changed = kept.With(taking);
            if (!changed.HasValue()) {
                return changed.Failure();
            }
            kept = changed.Value();
            return {};
        }
        return core::Error{
            core::ErrorKind::kNotFound, "lesson_not_found", "такого занятия здесь нет"};
    }

    core::Result<void> Save(const Lesson& lesson) override {
        for (const auto& other : kept_) {
            if (other.Tenant() == lesson.Tenant() && other.Tutor() == lesson.Tutor() &&
                Busy(other) && Busy(lesson) &&
                Overlaps(other, lesson, core::Instant::Duration::zero())) {
                return core::Error{core::ErrorKind::kConflict,
                                   "slot_already_taken",
                                   "это время у репетитора уже занято"};
            }
        }
        kept_.push_back(lesson);
        return {};
    }

    const std::vector<Lesson>& Kept() const noexcept {
        return kept_;
    }

private:
    /// Та же строка на новом месте: перенос и смена состояния не заводят
    /// второго занятия — ровно как `update` в базе.
    core::Result<void> Replace(const Lesson& lesson) {
        for (auto& kept : kept_) {
            if (kept.Tenant() == lesson.Tenant() && kept.Id() == lesson.Id()) {
                kept = lesson;
                return {};
            }
        }
        return core::Error{
            core::ErrorKind::kNotFound, "lesson_not_found", "такого занятия здесь нет"};
    }

    /// Занятие, занимающее слот. Отменённое и не состоявшееся его освобождают —
    /// ровно как условие `where` у ограничения базы.
    static bool Busy(const Lesson& lesson) noexcept {
        return lesson.State() == LessonState::kPlanned || lesson.State() == LessonState::kConfirmed;
    }

    template<class Match>
    std::vector<Lesson> Within(const core::TimeRange& window, Match match) const {
        std::vector<Lesson> found;
        for (const auto& lesson : kept_) {
            if (match(lesson) && window.Contains(lesson.StartsAt())) {
                found.push_back(lesson);
            }
        }
        std::sort(found.begin(), found.end(), [](const Lesson& left, const Lesson& right) {
            return left.StartsAt() < right.StartsAt();
        });
        return found;
    }

    std::vector<Lesson> kept_;
};

/// История занятия в памяти. Пишется только вперёд — как и в базе.
class FakeLessonHistory final : public ports::LessonHistory {
public:
    core::Result<void> Record(const LessonHistoryEntry& entry) override {
        kept_.push_back(entry);
        return {};
    }

    std::vector<LessonHistoryEntry> Of(const core::TenantId& tenant,
                                       const core::LessonId& lesson) const override {
        std::vector<LessonHistoryEntry> found;
        for (const auto& entry : kept_) {
            if (entry.tenant == tenant && entry.lesson == lesson) {
                found.push_back(entry);
            }
        }
        std::stable_sort(found.begin(),
                         found.end(),
                         [](const LessonHistoryEntry& left, const LessonHistoryEntry& right) {
                             return left.at < right.at;
                         });
        return found;
    }

private:
    std::vector<LessonHistoryEntry> kept_;
};

/// Доступность в памяти. Записывается целиком — как и в базе.
class FakeAvailability final : public ports::AvailabilityRepository {
public:
    std::optional<Availability> Of(const core::TenantId& tenant,
                                   const core::PersonId& tutor) const override {
        for (const auto& kept : kept_) {
            if (kept.tenant == tenant && kept.tutor == tutor) {
                return kept.availability;
            }
        }
        return std::nullopt;
    }

    core::Result<void> Replace(const core::TenantId& tenant,
                               const core::PersonId& tutor,
                               const Availability& availability) override {
        for (auto& kept : kept_) {
            if (kept.tenant == tenant && kept.tutor == tutor) {
                kept.availability = availability;
                return {};
            }
        }
        kept_.push_back(Kept{tenant, tutor, availability});
        return {};
    }

private:
    struct Kept final {
        core::TenantId tenant;
        core::PersonId tutor;
        Availability availability;
    };

    std::vector<Kept> kept_;
};

/// Серии в памяти. Хранит правило, а не развёрнутый список, — как и база.
class FakeSeries final : public ports::RecurrenceRepository {
public:
    core::Result<void> Create(const RecurrenceSeries& series) override {
        kept_.push_back(series);
        return {};
    }

    std::optional<RecurrenceSeries> Find(const core::TenantId& tenant,
                                         const core::SeriesId& id) const override {
        for (const auto& series : kept_) {
            if (series.Tenant() == tenant && series.Id() == id) {
                return series;
            }
        }
        return std::nullopt;
    }

    std::vector<core::SeriesId> Of(const core::TenantId& tenant,
                                   const core::PersonId& person) const override {
        std::vector<core::SeriesId> found;
        for (const auto& series : kept_) {
            if (series.Tenant() != tenant) {
                continue;
            }
            const auto& people = series.Participants();
            if (series.Tutor() == person ||
                std::find(people.begin(), people.end(), person) != people.end()) {
                found.push_back(series.Id());
            }
        }
        return found;
    }

    core::Result<void> Record(const core::TenantId& tenant,
                              const core::SeriesId& id,
                              const RecurrenceException& exception) override {
        for (auto& series : kept_) {
            if (series.Tenant() != tenant || series.Id() != id) {
                continue;
            }
            auto grown = series.With(exception);
            if (!grown.HasValue()) {
                return grown.Failure();
            }
            series = grown.Value();
            return {};
        }
        return core::Error{core::ErrorKind::kNotFound,
                           "recurrence_series_not_found",
                           "серии с таким идентификатором нет"};
    }

    core::Result<void> Reshape(const RecurrenceSeries& series) override {
        for (auto& kept : kept_) {
            if (kept.Tenant() == series.Tenant() && kept.Id() == series.Id()) {
                kept = series;
                return {};
            }
        }
        return core::Error{core::ErrorKind::kNotFound,
                           "recurrence_series_not_found",
                           "серии с таким идентификатором нет"};
    }

private:
    std::vector<RecurrenceSeries> kept_;
};

/// Перерывы в памяти.
///
/// ОТКАЗЫВАЕТ ТАМ ЖЕ, ГДЕ ОТКАЖЕТ БАЗА: два наложенных перерыва у одного
/// человека запрещает `scheduling_time_off_no_overlap`, и фейк, принимающий их
/// молча, делает unit-прогон зелёным на поведении, которого в проде нет.
class FakeTimeOffs final : public ports::TimeOffRepository {
public:
    core::Result<void> Save(const TimeOff& period, core::Instant declared_at) override {
        for (const auto& kept : kept_) {
            if (kept.period.Tenant() == period.Tenant() &&
                kept.period.Person() == period.Person() &&
                !(kept.period.To() < period.From() || period.To() < kept.period.From())) {
                return core::Error{core::ErrorKind::kConflict,
                                   "time_off_overlaps",
                                   "на эти дни у человека уже заведён перерыв"};
            }
        }
        kept_.push_back(Kept{period, declared_at, std::nullopt});
        return {};
    }

    std::optional<TimeOff> Find(const core::TenantId& tenant,
                                const core::TimeOffId& id) const override {
        for (const auto& kept : kept_) {
            if (kept.period.Tenant() == tenant && kept.period.Id() == id) {
                return kept.period;
            }
        }
        return std::nullopt;
    }

    core::Result<void> Decide(const core::TenantId& tenant,
                              const core::TimeOffId& id,
                              TimeOffDecision lessons,
                              SeriesDecision series,
                              core::Instant at) override {
        for (auto& kept : kept_) {
            if (kept.period.Tenant() != tenant || kept.period.Id() != id) {
                continue;
            }
            if (kept.decided.has_value()) {
                return core::Error{core::ErrorKind::kConflict,
                                   "time_off_already_decided",
                                   "по этому перерыву решение уже принято: занятия отменены или "
                                   "перенесены, и второй раз применять его не к чему"};
            }
            kept.decided = Decision{lessons, series, at};
            return {};
        }
        return core::Error{
            core::ErrorKind::kNotFound, "time_off_not_found", "такого перерыва здесь нет"};
    }

    struct Decision final {
        TimeOffDecision lessons{TimeOffDecision::kKeep};
        SeriesDecision series{SeriesDecision::kSkip};
        core::Instant at;
    };

    std::optional<Decision> DecisionOn(const core::TimeOffId& id) const {
        for (const auto& kept : kept_) {
            if (kept.period.Id() == id) {
                return kept.decided;
            }
        }
        return std::nullopt;
    }

private:
    struct Kept final {
        TimeOff period;
        core::Instant declared_at;
        std::optional<Decision> decided;
    };

    std::vector<Kept> kept_;
};

/// Местные дни в моменты — по ОДНОМУ СМЕЩЕНИЮ на всю историю.
///
/// Настоящий ответ считает база: таблицы переводов часов у ядра нет намеренно.
/// Фейк переводов не знает вовсе, и это честнее, чем изображать их наполовину:
/// что перевод часов сдвигает границу дня, проверяет живой набор против
/// настоящего Postgres, а не этот класс.
class FakeLocalDays final : public ports::LocalDays {
public:
    explicit FakeLocalDays(core::Instant::Duration offset = std::chrono::hours{0}) noexcept
        : offset_{offset} {}

    core::Result<core::TimeRange> Between(const core::Date& from,
                                          const core::Date& to,
                                          const core::TimeZone&) const override {
        if (to < from) {
            return core::Error{core::ErrorKind::kValidation,
                               "local_days_backwards",
                               "конец отрезка дней раньше его начала"};
        }
        const auto midnight = [this](const core::Date& date) {
            const core::LocalDateTime local{date, core::LocalTime::Compose(0, 0).Value()};
            return core::Instant::FromUnixMicros((local.AsIfUtc() - offset_).count());
        };

        const std::chrono::year_month_day next{
            std::chrono::sys_days{std::chrono::year_month_day{std::chrono::year{to.Year()},
                                                              std::chrono::month{to.Month()},
                                                              std::chrono::day{to.Day()}}} +
            std::chrono::days{1}};
        const auto after = core::Date::Compose(static_cast<int>(next.year()),
                                               static_cast<unsigned>(next.month()),
                                               static_cast<unsigned>(next.day()));

        return core::TimeRange::Compose(midnight(from), midnight(after.Value()));
    }

private:
    core::Instant::Duration offset_;
};

}  // namespace pdr::scheduling::testing
