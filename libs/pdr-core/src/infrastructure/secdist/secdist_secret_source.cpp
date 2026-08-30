#include "infrastructure/secdist/secdist_secret_source.hpp"

#include <string>

namespace pdr::infrastructure {

PdrSecrets::PdrSecrets(const userver::formats::json::Value& document) {
    const auto section = document[std::string{SecdistSecretSource::kSection}];
    if (!section.IsObject()) {
        return;
    }
    for (auto item = section.begin(); item != section.end(); ++item) {
        if (item->IsString()) {
            values.emplace(item.GetName(), item->As<std::string>());
        }
    }
}

SecdistSecretSource::SecdistSecretSource(const userver::components::Secdist& secdist) noexcept
    : secdist_{secdist} {}

std::optional<core::SecretString> SecdistSecretSource::Find(std::string_view name) const {
    const auto& secrets = secdist_.Get().Get<PdrSecrets>();

    const auto found = secrets.values.find(std::string{name});
    if (found == secrets.values.end()) {
        return std::nullopt;
    }
    return core::SecretString{found->second};
}

}  // namespace pdr::infrastructure
