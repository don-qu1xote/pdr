#include "scheduling/infrastructure/http/availability_operations.hpp"

#include <utility>

#include <userver/components/component.hpp>

#include "scheduling/application/get_availability.hpp"
#include "scheduling/application/set_availability.hpp"
#include "scheduling/infrastructure/http/api_mapping.hpp"
#include "scheduling/infrastructure/http/arguments.hpp"
#include "scheduling/infrastructure/postgres_availability_repository.hpp"

namespace pdr::scheduling::http {

GetAvailabilityHandler::GetAvailabilityHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      ids_{parts.Ids()} {}

identity::Action GetAvailabilityHandler::Wants() const {
    return identity::Action::kViewSchedule;
}

identity::Resource GetAvailabilityHandler::About(const userver::server::http::HttpRequest& request,
                                                 const infrastructure::http::Caller& caller,
                                                 const api::Nothing&) const {
    return HoursOf(request, caller);
}

core::Result<api::Availability> GetAvailabilityHandler::Run(const Call& call) const {
    const auto whose = Whose(call.request, call.caller.actor);
    if (!whose.HasValue()) {
        return whose.Failure();
    }

    PostgresAvailabilityRepository availability{call.session, ids_};
    const GetAvailability showing{availability};

    const auto found = showing.Execute(GetAvailability::Request{call.caller.tenant, whose.Value()});
    if (!found.HasValue()) {
        return found.Failure();
    }
    return AsAnswer(found.Value());
}

SetAvailabilityHandler::SetAvailabilityHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      ids_{parts.Ids()} {}

identity::Action SetAvailabilityHandler::Wants() const {
    return identity::Action::kSetAvailability;
}

identity::Resource SetAvailabilityHandler::About(const userver::server::http::HttpRequest& request,
                                                 const infrastructure::http::Caller& caller,
                                                 const api::Availability&) const {
    return HoursOf(request, caller);
}

core::Result<api::Availability> SetAvailabilityHandler::Run(const Call& call) const {
    const auto whose = Whose(call.request, call.caller.actor);
    if (!whose.HasValue()) {
        return whose.Failure();
    }

    auto asked = AsDomain(call.body);
    if (!asked.HasValue()) {
        return asked.Failure();
    }

    PostgresAvailabilityRepository availability{call.session, ids_};
    const SetAvailability writing{availability};

    const auto written =
        writing.Execute(SetAvailability::Request{call.caller.tenant, whose.Value(), asked.Value()});
    if (!written.HasValue()) {
        return written.Failure();
    }
    return AsAnswer(asked.Value());
}

GetAvailabilityOperation::GetAvailabilityOperation(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& GetAvailabilityOperation::Handler() const {
    return handler_;
}

SetAvailabilityOperation::SetAvailabilityOperation(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& SetAvailabilityOperation::Handler() const {
    return handler_;
}

userver::yaml_config::Schema GetAvailabilityOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

userver::yaml_config::Schema SetAvailabilityOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

}  // namespace pdr::scheduling::http
