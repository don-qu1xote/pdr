#pragma once

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "scheduling/application/ports/availability_repository.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"
#include "scheduling/core/overlap.hpp"

namespace pdr::scheduling::testing {

/// Занятия в памяти.
///
/// ОТКАЗЫВАЕТ ТАМ ЖЕ, ГДЕ ОТКАЖЕТ БАЗА. Пересечение у репетитора запрещено
/// ограничением `scheduling_lesson_no_overlap`; фейк, принимающий такое занятие
/// молча, делает unit-прогон зелёным на поведении, которого в проде нет. Что
/// обе реализации отказывают одинаково, проверяет contract-набор.
class FakeLessons final : public ports::LessonRepository {
public:
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
            const auto& people = lesson.Participants();
            return lesson.Tenant() == tenant &&
                   std::find(people.begin(), people.end(), participant) != people.end();
        });
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

private:
    std::vector<RecurrenceSeries> kept_;
};

}  // namespace pdr::scheduling::testing
