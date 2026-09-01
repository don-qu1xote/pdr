#include "infrastructure/db/unscoped_access.hpp"

namespace pdr::infrastructure::db {

std::string_view Name(UnscopedReason reason) noexcept {
    switch (reason) {
        case UnscopedReason::kClusterWideJobLock:
            return "cluster_wide_job_lock";
        case UnscopedReason::kClusterWideJobJournal:
            return "cluster_wide_job_journal";
        case UnscopedReason::kSchemaMigration:
            return "schema_migration";
        case UnscopedReason::kPlatformWideIdentity:
            return "platform_wide_identity";
        case UnscopedReason::kReadinessProbe:
            return "readiness_probe";
        case UnscopedReason::kOperatorExport:
            return "operator_export";
    }
    return "cluster_wide_job_lock";
}

}  // namespace pdr::infrastructure::db
