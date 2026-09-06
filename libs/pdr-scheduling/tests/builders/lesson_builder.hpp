#pragma once

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/participation.hpp"

namespace pdr::scheduling::testing {

/// Билдер занятия: `LessonBuilder{}.InTenant(t).StartingAt(m).Lasting(60min).Build()`.
///
/// Лежит в модуле scheduling: доменный билдер живёт рядом со своим доменом,
/// потому что платформенная оснастка не имеет права зависеть от контекста
/// (docs/testing.md).
class LessonBuilder final {
public:
    LessonBuilder& Id(core::LessonId id) noexcept {
        id_ = id;
        return *this;
    }

    LessonBuilder& InTenant(core::TenantId tenant) noexcept {
        tenant_ = tenant;
        return *this;
    }

    LessonBuilder& Between(core::PersonId tutor, core::PersonId student) {
        tutor_ = tutor;
        students_ = {std::move(student)};
        return *this;
    }

    LessonBuilder& StartingAt(core::Instant starts_at) noexcept {
        starts_at_ = starts_at;
        return *this;
    }

    LessonBuilder& Lasting(Lesson::Duration duration) noexcept {
        duration_ = duration;
        return *this;
    }

    /// «Сейчас» для проверки «занятие не в прошлом». По умолчанию — сутки до
    /// начала: занятие назначено заранее, как оно и бывает.
    /// Зона, в которой занятие задумано. По умолчанию Москва — рынок
    /// российский, и зона без переводов часов делает ожидаемые значения
    /// очевидными в любом тесте, которому она безразлична.
    LessonBuilder& InZone(core::TimeZone zone) noexcept {
        zone_ = std::move(zone);
        return *this;
    }

    LessonBuilder& AsOf(core::Instant now) noexcept {
        now_ = now;
        return *this;
    }

    /// Кого записывать. Список, а не один: занятие держит участия, и проверке,
    /// которой нужны двое, не приходится собирать занятие руками.
    LessonBuilder& Between(core::PersonId tutor, std::vector<core::PersonId> students) {
        tutor_ = std::move(tutor);
        students_ = std::move(students);
        return *this;
    }

    Lesson Build() const {
        const auto now = now_.has_value() ? *now_ : starts_at_ - std::chrono::hours{24};

        std::vector<Participation> taking;
        taking.reserve(students_.size());
        for (const auto& student : students_) {
            taking.push_back(Participation::Joined(student));
        }

        auto lesson = Lesson::Schedule(
            id_, tenant_, tutor_, std::move(taking), starts_at_, duration_, zone_, now);
        if (!lesson.HasValue()) {
            throw std::logic_error{std::string{"LessonBuilder: "} + lesson.Failure().Code()};
        }
        return lesson.Value();
    }

private:
    core::LessonId id_{pdr::testing::Numbered<core::LessonId>(100)};
    core::TenantId tenant_{pdr::testing::Numbered<core::TenantId>(1)};
    core::PersonId tutor_{pdr::testing::Numbered<core::PersonId>(10)};
    std::vector<core::PersonId> students_{pdr::testing::Numbered<core::PersonId>(20)};
    core::Instant starts_at_{pdr::testing::MomentBuilder{}.Utc(2026, 3, 2).At(18, 0).Build()};
    Lesson::Duration duration_{std::chrono::minutes{60}};
    core::TimeZone zone_{*core::TimeZone::Parse("Europe/Moscow")};
    std::optional<core::Instant> now_;
};

}  // namespace pdr::scheduling::testing
