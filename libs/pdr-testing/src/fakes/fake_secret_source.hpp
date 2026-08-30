#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "application/ports/secret_source.hpp"

namespace pdr::testing {

/// Единственный фейк источника секретов.
///
/// Отличает «нет вовсе» от «есть и пусто» так же, как настоящий: без этого
/// проверка «пустой секрет роняет старт» проходила бы по неправильной причине.
class FakeSecretSource final : public application::ports::SecretSource {
public:
    FakeSecretSource& Put(std::string name, std::string value) {
        known_.insert_or_assign(std::move(name), std::move(value));
        return *this;
    }

    FakeSecretSource& Forget(std::string_view name) {
        known_.erase(std::string{name});
        return *this;
    }

    std::optional<core::SecretString> Find(std::string_view name) const override {
        const auto found = known_.find(std::string{name});
        if (found == known_.end()) {
            return std::nullopt;
        }
        return core::SecretString{found->second};
    }

private:
    std::map<std::string, std::string> known_;
};

}  // namespace pdr::testing
