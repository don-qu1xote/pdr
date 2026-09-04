#include "infrastructure/observe/span_tags.hpp"

#include <string>

#include <userver/tracing/span.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::infrastructure::observe {
namespace {

void Tag(const std::string& field, const std::string& value) {
    auto* span = userver::tracing::Span::CurrentSpanUnchecked();
    if (span == nullptr) {
        return;
    }
    span->AddTag(field, value);
}

}  // namespace

void TagTenant(const core::TenantId& tenant) {
    Tag(kTenantField, tenant.ToString());
}

void TagActor(const core::PersonId& actor) {
    if (actor == core::PersonId::FromBytes(core::IdBytes{})) {
        return;
    }
    Tag(kActorField, actor.ToString());
}

}  // namespace pdr::infrastructure::observe
