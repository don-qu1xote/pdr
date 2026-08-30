#include "application/verify_secrets.hpp"

#include <utility>
#include <vector>

namespace pdr::application {
namespace {

std::string Missing(std::string_view name) {
    return "секрета «" + std::string{name} +
           "» нет вовсе. Значений по умолчанию у секретов не существует: "
           "умолчание доживает до прода и работает";
}

std::string Blank(std::string_view name) {
    return "секрет «" + std::string{name} +
           "» пуст. Пустая строка — не значение, а забытая переменная";
}

std::string Shared(std::string_view name, std::string_view other, bool same_purpose) {
    std::string reason =
        "секрет «" + std::string{name} + "» совпадает с «" + std::string{other} + "»";
    if (same_purpose) {
        return reason +
               ". Два разных секрета с одним значением — это вставленное дважды из "
               "буфера обмена";
    }
    return reason +
           ", а назначения у них разные. Утечка одного тогда равна утечке обоих: ключ, которым "
           "ходим мы, и ключ, которым подписывают нам, обязаны различаться";
}

}  // namespace

std::vector<SecretComplaint> InspectSecrets(std::span<const core::SecretSpec> registry,
                                            const ports::SecretSource& source) {
    std::vector<SecretComplaint> complaints;

    std::vector<std::pair<core::SecretSpec, core::SecretString>> taken;

    for (const auto& spec : registry) {
        if (!spec.RequiredNow()) {
            continue;
        }

        auto found = source.Find(spec.name);
        if (!found.has_value()) {
            complaints.push_back(SecretComplaint{std::string{spec.name}, Missing(spec.name)});
            continue;
        }
        if (found->Empty()) {
            complaints.push_back(SecretComplaint{std::string{spec.name}, Blank(spec.name)});
            continue;
        }

        bool shared = false;
        for (const auto& [earlier, value] : taken) {
            if (value == *found) {
                complaints.push_back(SecretComplaint{
                    std::string{spec.name},
                    Shared(spec.name, earlier.name, earlier.purpose == spec.purpose)});
                shared = true;
                break;
            }
        }
        if (!shared) {
            taken.emplace_back(spec, std::move(*found));
        }
    }

    return complaints;
}

core::Result<void> VerifySecrets(std::span<const core::SecretSpec> registry,
                                 const ports::SecretSource& source) {
    const auto complaints = InspectSecrets(registry, source);
    if (complaints.empty()) {
        return {};
    }

    std::string detail;
    for (const auto& complaint : complaints) {
        if (!detail.empty()) {
            detail += "; ";
        }
        detail += complaint.reason;
    }

    return core::Error{core::ErrorKind::kValidation, "secrets_incomplete", std::move(detail)};
}

}  // namespace pdr::application
