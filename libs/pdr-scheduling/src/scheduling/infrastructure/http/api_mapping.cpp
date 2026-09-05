#include "scheduling/infrastructure/http/api_mapping.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <utility>

namespace pdr::scheduling::http {
namespace {

constexpr std::size_t kDateLength = 10;
constexpr std::size_t kClockLength = 5;

core::Error Malformed(const std::string& what) {
    return core::Error{
        core::ErrorKind::kValidation, "calendar_text_malformed", "не удалось разобрать " + what};
}

/// Число из куска строки. Образец схемы уже сказал, что здесь только цифры, —
/// остаётся перевести их в число и отдать разбор дальше домену.
bool Number(std::string_view text, unsigned& into) {
    const auto* end = text.data() + text.size();
    const auto read = std::from_chars(text.data(), end, into);
    return read.ec == std::errc{} && read.ptr == end;
}

api::LessonState AsAnswer(LessonState state) noexcept {
    switch (state) {
        case LessonState::kPlanned:
            return api::LessonState::kPlanned;
        case LessonState::kConfirmed:
            return api::LessonState::kConfirmed;
        case LessonState::kHeld:
            return api::LessonState::kHeld;
        case LessonState::kCancelled:
            return api::LessonState::kCancelled;
        case LessonState::kNoShow:
            return api::LessonState::kNoShow;
        case LessonState::kBoundary:
            break;
    }
    return api::LessonState::kPlanned;
}

std::vector<boost::uuids::uuid> AsUuids(const std::vector<core::PersonId>& people) {
    std::vector<boost::uuids::uuid> written;
    written.reserve(people.size());
    for (const auto& person : people) {
        written.push_back(AsUuid(person.AsBytes()));
    }
    return written;
}

std::string Padded(unsigned value, std::size_t width) {
    auto text = std::to_string(value);
    return text.size() >= width ? text : std::string(width - text.size(), '0') + text;
}

}  // namespace

core::PersonId AsPerson(const boost::uuids::uuid& value) noexcept {
    core::IdBytes bytes{};
    std::copy(value.begin(), value.end(), bytes.begin());
    return core::PersonId::FromBytes(bytes);
}

core::LessonId AsLesson(const boost::uuids::uuid& value) noexcept {
    core::IdBytes bytes{};
    std::copy(value.begin(), value.end(), bytes.begin());
    return core::LessonId::FromBytes(bytes);
}

boost::uuids::uuid AsUuid(const core::IdBytes& bytes) noexcept {
    boost::uuids::uuid written{};
    std::copy(bytes.begin(), bytes.end(), written.begin());
    return written;
}

core::Result<core::TimeZone> AsZone(const std::string& name) {
    auto zone = core::TimeZone::Parse(name);
    if (!zone.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_zone_unknown",
                           "«" + name + "» не похоже на имя часового пояса"};
    }
    return std::move(*zone);
}

core::Result<core::Date> AsDate(const std::string& text) {
    unsigned year{};
    unsigned month{};
    unsigned day{};
    if (text.size() != kDateLength || !Number(std::string_view{text}.substr(0, 4), year) ||
        !Number(std::string_view{text}.substr(5, 2), month) ||
        !Number(std::string_view{text}.substr(8, 2), day)) {
        return Malformed("дату «" + text + "»");
    }
    return core::Date::Compose(static_cast<int>(year), month, day);
}

core::Result<core::LocalTime> AsClock(const std::string& text) {
    unsigned hour{};
    unsigned minute{};
    if (text.size() != kClockLength || !Number(std::string_view{text}.substr(0, 2), hour) ||
        !Number(std::string_view{text}.substr(3, 2), minute)) {
        return Malformed("время «" + text + "»");
    }
    return core::LocalTime::Compose(hour, minute);
}

std::string FormatDate(const core::Date& date) {
    return Padded(static_cast<unsigned>(date.Year()), 4) + "-" + Padded(date.Month(), 2) + "-" +
           Padded(date.Day(), 2);
}

std::string FormatClock(core::LocalTime time) {
    return Padded(time.Hour(), 2) + ":" + Padded(time.Minute(), 2);
}

Side AsSide(api::LessonSide side) noexcept {
    return side == api::LessonSide::kTutor ? Side::kTutor : Side::kParticipant;
}

api::Lesson AsAnswer(const Lesson& lesson) {
    return api::Lesson{AsUuid(lesson.Id().AsBytes()),
                       AsUuid(lesson.Tutor().AsBytes()),
                       AsUuids(lesson.Participants()),
                       lesson.StartsAt().UnixMicros(),
                       lesson.EndsAt().UnixMicros(),
                       lesson.Zone().Name(),
                       AsAnswer(lesson.State())};
}

api::Lessons AsAnswer(const std::vector<Lesson>& lessons) {
    api::Lessons written;
    written.lessons.reserve(lessons.size());
    for (const auto& lesson : lessons) {
        written.lessons.push_back(AsAnswer(lesson));
    }
    return written;
}

api::Series AsAnswer(const RecurrenceSeries& series) {
    return api::Series{AsUuid(series.Id().AsBytes()),
                       AsUuid(series.Tutor().AsBytes()),
                       AsUuids(series.Participants()),
                       series.Rule().ToRRule(),
                       FormatDate(series.StartsOn()),
                       FormatClock(series.At()),
                       static_cast<int>(series.LessonDuration().count()),
                       series.Zone().Name()};
}

api::Availability AsAnswer(const Availability& availability) {
    api::Availability written;

    written.rules.reserve(availability.Rules().size());
    for (const auto& rule : availability.Rules()) {
        written.rules.push_back(api::AvailabilityRule{static_cast<int>(rule.Day()),
                                                      FormatClock(rule.From()),
                                                      FormatClock(rule.To()),
                                                      rule.Zone().Name()});
    }

    written.exceptions.reserve(availability.Exceptions().size());
    for (const auto& exception : availability.Exceptions()) {
        api::AvailabilityException day{FormatDate(exception.date), std::nullopt, std::nullopt};
        if (exception.instead.has_value()) {
            day.from = exception.instead->From().UnixMicros();
            day.to = exception.instead->To().UnixMicros();
        }
        written.exceptions.push_back(std::move(day));
    }

    return written;
}

core::Result<Availability> AsDomain(const api::Availability& availability) {
    std::vector<AvailabilityRule> rules;
    rules.reserve(availability.rules.size());
    for (const auto& rule : availability.rules) {
        const auto from = AsClock(rule.from);
        if (!from.HasValue()) {
            return from.Failure();
        }
        const auto to = AsClock(rule.to);
        if (!to.HasValue()) {
            return to.Failure();
        }
        const auto zone = AsZone(rule.tz);
        if (!zone.HasValue()) {
            return zone.Failure();
        }

        const auto composed = AvailabilityRule::Compose(
            static_cast<core::Weekday>(rule.weekday), from.Value(), to.Value(), zone.Value());
        if (!composed.HasValue()) {
            return composed.Failure();
        }
        rules.push_back(composed.Value());
    }

    std::vector<AvailabilityException> exceptions;
    exceptions.reserve(availability.exceptions.size());
    for (const auto& day : availability.exceptions) {
        const auto date = AsDate(day.date);
        if (!date.HasValue()) {
            return date.Failure();
        }

        std::optional<core::TimeRange> instead;
        if (day.from.has_value() != day.to.has_value()) {
            return core::Error{core::ErrorKind::kValidation,
                               "availability_exception_half_open",
                               "у дня с иными часами названы обе границы или ни одной"};
        }
        if (day.from.has_value()) {
            const auto range = core::TimeRange::Compose(core::Instant::FromUnixMicros(*day.from),
                                                        core::Instant::FromUnixMicros(*day.to));
            if (!range.HasValue()) {
                return range.Failure();
            }
            instead = range.Value();
        }

        exceptions.push_back(AvailabilityException{date.Value(), instead});
    }

    return Availability::Compose(std::move(rules), std::move(exceptions));
}

core::Error BadArgument(std::string_view name) {
    return core::Error{core::ErrorKind::kValidation,
                       "request_argument_invalid",
                       "аргумент «" + std::string{name} + "» в адресе не разобрался"};
}

}  // namespace pdr::scheduling::http
