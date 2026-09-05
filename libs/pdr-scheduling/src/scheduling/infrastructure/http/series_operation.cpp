#include "scheduling/infrastructure/http/series_operation.hpp"

#include <chrono>
#include <utility>

#include <userver/components/component.hpp>

#include "scheduling/application/create_series.hpp"
#include "scheduling/infrastructure/http/api_mapping.hpp"
#include "scheduling/infrastructure/postgres_recurrence_repository.hpp"

namespace pdr::scheduling::http {

CreateSeriesHandler::CreateSeriesHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      ids_{parts.Ids()} {}

identity::Action CreateSeriesHandler::Wants() const {
    return identity::Action::kBookLesson;
}

identity::Resource CreateSeriesHandler::About(const userver::server::http::HttpRequest&,
                                              const infrastructure::http::Caller& caller,
                                              const api::NewSeries& body) const {
    return identity::Resource{caller.tenant, AsPerson(body.tutor), AsPerson(body.student)};
}

core::Result<api::Series> CreateSeriesHandler::Run(const Call& call) const {
    const auto starts_on = AsDate(call.body.starts_on);
    if (!starts_on.HasValue()) {
        return starts_on.Failure();
    }
    const auto at = AsClock(call.body.at);
    if (!at.HasValue()) {
        return at.Failure();
    }
    const auto zone = AsZone(call.body.tz);
    if (!zone.HasValue()) {
        return zone.Failure();
    }

    PostgresRecurrenceRepository series{call.session};
    const CreateSeries creating{series, ids_};

    const auto created =
        creating.Execute(CreateSeries::Request{call.caller.tenant,
                                               AsPerson(call.body.tutor),
                                               AsPerson(call.body.student),
                                               call.body.rrule,
                                               starts_on.Value(),
                                               at.Value(),
                                               zone.Value(),
                                               std::chrono::minutes{call.body.minutes}});
    if (!created.HasValue()) {
        return created.Failure();
    }
    return AsAnswer(created.Value());
}

CreateSeriesOperation::CreateSeriesOperation(const userver::components::ComponentConfig& config,
                                             const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& CreateSeriesOperation::Handler() const {
    return handler_;
}

userver::yaml_config::Schema CreateSeriesOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

}  // namespace pdr::scheduling::http
