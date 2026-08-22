#pragma once

#include <chrono>
#include <stdexcept>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling::testing {

/// Билдер занятия: `LessonBuilder{}.InTenant(t).StartingAt(m).Lasting(60min).Build()`.
///
/// Лежит в модуле scheduling: доменный билдер живёт рядом со своим доменом,
/// потому что платформенная оснастка не имеет права зависеть от контекста
/// (docs/architecture/testing.md).
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

    LessonBuilder& Between(core::PersonId tutor, core::PersonId student) noexcept {
        tutor_ = tutor;
        student_ = student;
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
    LessonBuilder& AsOf(core::Instant now) noexcept {
        now_ = now;
        return *this;
    }

    Lesson Build() const {
        const auto now = now_.has_value() ? *now_ : starts_at_ - std::chrono::hours{24};
        auto lesson = Lesson::Schedule(id_, tenant_, tutor_, student_, starts_at_, duration_, now);
        if (!lesson.HasValue()) {
            throw std::logic_error{"LessonBuilder: " + lesson.Failure().Code()};
        }
        return lesson.Value();
    }

private:
    core::LessonId id_{pdr::testing::Numbered<core::LessonId>(100)};
    core::TenantId tenant_{pdr::testing::Numbered<core::TenantId>(1)};
    core::PersonId tutor_{pdr::testing::Numbered<core::PersonId>(10)};
    core::PersonId student_{pdr::testing::Numbered<core::PersonId>(20)};
    core::Instant starts_at_{pdr::testing::MomentBuilder{}.Utc(2026, 3, 2).At(18, 0).Build()};
    Lesson::Duration duration_{std::chrono::minutes{60}};
    std::optional<core::Instant> now_;
};

}  // namespace pdr::scheduling::testing
