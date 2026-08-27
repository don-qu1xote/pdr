#include "identity/application/manage_publication.hpp"

#include "events/identity/practice_visibility_changed.hpp"

namespace pdr::identity {
namespace {

core::Error NoPractice() {
    return core::Error{core::ErrorKind::kNotFound, "practice_unknown", "такой практики нет"};
}

void Announce(events::Bus& bus, const Practice& practice, core::Instant at) {
    bus.Publish(pdr::events::identity::PracticeVisibilityChanged{
        pdr::events::Envelope{practice.Tenant(), at},
        Name(practice.Visible()),
        practice.IsDiscoverable(),
    });
}

}  // namespace

AskToPublish::AskToPublish(ports::Practices& practices,
                           const application::ports::Clock& clock,
                           events::Bus& bus) noexcept
    : practices_{practices}, clock_{clock}, bus_{bus} {}

core::Result<Practice> AskToPublish::Execute(const core::TenantId& tenant) const {
    const auto found = practices_.Find(tenant);
    if (!found.has_value()) {
        return NoPractice();
    }

    const auto asked = found->AskedToPublish(clock_.Now());
    if (!asked) {
        return asked.Failure();
    }

    practices_.Save(asked.Value());
    Announce(bus_, asked.Value(), clock_.Now());
    return asked.Value();
}

core::Result<Practice> AskToPublish::Hide(const core::TenantId& tenant) const {
    const auto found = practices_.Find(tenant);
    if (!found.has_value()) {
        return NoPractice();
    }

    const auto hidden = found->Hidden(clock_.Now());
    practices_.Save(hidden);
    Announce(bus_, hidden, clock_.Now());
    return hidden;
}

DecidePublication::DecidePublication(ports::Practices& practices,
                                     const application::ports::Clock& clock,
                                     events::Bus& bus) noexcept
    : practices_{practices}, clock_{clock}, bus_{bus} {}

core::Result<Practice> DecidePublication::Publish(const core::TenantId& tenant) const {
    const auto found = practices_.Find(tenant);
    if (!found.has_value()) {
        return NoPractice();
    }

    const auto published = found->Published(clock_.Now());
    if (!published) {
        return published.Failure();
    }

    practices_.Save(published.Value());
    Announce(bus_, published.Value(), clock_.Now());
    return published.Value();
}

core::Result<Practice> DecidePublication::Refuse(const core::TenantId& tenant,
                                                 RefusalReason reason) const {
    const auto found = practices_.Find(tenant);
    if (!found.has_value()) {
        return NoPractice();
    }

    const auto refused = found->RefusedBecause(reason, clock_.Now());
    if (!refused) {
        return refused.Failure();
    }

    practices_.Save(refused.Value());
    Announce(bus_, refused.Value(), clock_.Now());
    return refused.Value();
}

}  // namespace pdr::identity
