#include "scheduling/infrastructure/http/arguments.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

#include "scheduling/infrastructure/http/api_mapping.hpp"

namespace pdr::scheduling::http {
namespace {

constexpr std::string_view kTutorSide = "tutor";
constexpr std::string_view kParticipantSide = "participant";

std::optional<std::int64_t> AsNumber(const std::string& text) {
    std::int64_t value{};
    const auto* end = text.data() + text.size();
    const auto read = std::from_chars(text.data(), end, value);
    if (read.ec != std::errc{} || read.ptr != end) {
        return std::nullopt;
    }
    return value;
}

core::Result<core::Instant> Moment(const userver::server::http::HttpRequest& request,
                                   std::string_view name) {
    const auto value = AsNumber(request.GetArg(name));
    if (!value.has_value()) {
        return BadArgument(name);
    }
    return core::Instant::FromUnixMicros(*value);
}

}  // namespace

core::Result<core::PersonId> Whose(const userver::server::http::HttpRequest& request,
                                   const core::PersonId& asking) {
    if (!request.HasArg(kWhose)) {
        return asking;
    }

    const auto named = core::PersonId::Parse(request.GetArg(kWhose));
    if (!named.has_value()) {
        return BadArgument(kWhose);
    }
    return *named;
}

core::Result<Side> WhichSide(const userver::server::http::HttpRequest& request) {
    const auto& named = request.GetArg(kSide);
    if (named == kTutorSide) {
        return Side::kTutor;
    }
    if (named == kParticipantSide) {
        return Side::kParticipant;
    }
    return BadArgument(kSide);
}

core::Result<core::TimeRange> Window(const userver::server::http::HttpRequest& request) {
    const auto from = Moment(request, kFrom);
    if (!from.HasValue()) {
        return from.Failure();
    }
    const auto to = Moment(request, kTo);
    if (!to.HasValue()) {
        return to.Failure();
    }
    return core::TimeRange::Compose(from.Value(), to.Value());
}

core::Result<core::LessonId> WhichLesson(const userver::server::http::HttpRequest& request) {
    const auto named = core::LessonId::Parse(request.GetPathArg(kLesson));
    if (!named.has_value()) {
        return BadArgument(kLesson);
    }
    return *named;
}

identity::Resource HoursOf(const userver::server::http::HttpRequest& request,
                           const infrastructure::http::Caller& caller) {
    const auto whose = Whose(request, caller.actor);
    return identity::Resource{
        caller.tenant, whose.HasValue() ? whose.Value() : caller.actor, std::nullopt};
}

identity::Resource ScheduleOf(const userver::server::http::HttpRequest& request,
                              const infrastructure::http::Caller& caller) {
    const auto whose = Whose(request, caller.actor);
    const auto& person = whose.HasValue() ? whose.Value() : caller.actor;

    const auto side = WhichSide(request);
    if (side.HasValue() && side.Value() == Side::kTutor) {
        return identity::Resource{caller.tenant, person, std::nullopt};
    }
    return identity::Resource{caller.tenant, std::nullopt, person};
}

}  // namespace pdr::scheduling::http
