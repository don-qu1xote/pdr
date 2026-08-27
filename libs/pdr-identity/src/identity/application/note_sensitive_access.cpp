#include "identity/application/note_sensitive_access.hpp"

namespace pdr::identity {

NoteSensitiveAccess::NoteSensitiveAccess(ports::AccessLog& log,
                                         const application::ports::Clock& clock) noexcept
    : log_{log}, clock_{clock} {}

core::Result<void> NoteSensitiveAccess::Execute(const core::TenantId& tenant,
                                                const core::PersonId& actor,
                                                const core::PersonId& subject,
                                                ResourceKind kind,
                                                AccessOutcome outcome) const {
    auto record = AccessRecord::Of(tenant, actor, subject, kind, outcome, clock_.Now());
    if (!record) {
        return record.Failure();
    }

    log_.Record(record.Value());
    return {};
}

}  // namespace pdr::identity
