#include "identity/application/show_my_data.hpp"

#include <algorithm>

namespace pdr::identity {

ShowMyData::ShowMyData(const ports::Consents& consents,
                       const ports::PolicyVersions& versions,
                       const application::ports::Clock& clock) noexcept
    : consents_{consents}, versions_{versions}, clock_{clock} {}

core::Result<MyData> ShowMyData::Execute(const core::TenantId& tenant,
                                         const core::PersonId& subject) const {
    const auto now = clock_.Now();
    const auto current = versions_.Current();

    MyData shown{{}, {}, {}, false, current};
    shown.categories.assign(kEveryPersonalDataCategory.begin(), kEveryPersonalDataCategory.end());
    shown.recipients.assign(kEveryRecipient.begin(), kEveryRecipient.end());

    std::optional<PolicyVersion> accepted;
    for (const auto& record : consents_.Of(tenant, subject)) {
        shown.consents.push_back(ConsentShown{record.Kind(),
                                              record.Version(),
                                              record.Action(),
                                              record.GivenAt(),
                                              record.ByGuardian(),
                                              record.IsLiveAt(now)});

        if (record.Kind() != ConsentKind::kProcessing || !record.IsLiveAt(now)) {
            continue;
        }
        if (!accepted.has_value() || *accepted < record.Version()) {
            accepted = record.Version();
        }
    }

    shown.asks_to_accept_again =
        !accepted.has_value() ||
        NeedsReacceptance(*accepted, current, versions_.SubstantialAfter(*accepted));

    return shown;
}

}  // namespace pdr::identity
