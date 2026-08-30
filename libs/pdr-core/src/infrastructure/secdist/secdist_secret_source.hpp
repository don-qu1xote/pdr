#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <userver/formats/json/value.hpp>
#include <userver/storages/secdist/component.hpp>

#include "application/ports/secret_source.hpp"

namespace pdr::infrastructure {

/// Раздел файла секретов, в котором лежат секреты ПДР.
///
/// Разделы userver (`postgresql_settings` и прочие) он разбирает сам и по своим
/// правилам — сюда они не попадают. Имена внутри берутся из реестра
/// (`core::kEverySecret`), а не придумываются на месте.
struct PdrSecrets final {
    explicit PdrSecrets(const userver::formats::json::Value& document);

    std::unordered_map<std::string, std::string> values;
};

/// Секреты из штатного `components::Secdist` (ADR-0013).
///
/// Своего чтения файла здесь нет: права, перечитывание и разбор — забота
/// userver.
///
/// Окружение здесь не читается вовсе, и это правило, а не стиль: переменные
/// видны в `/proc`, попадают в дампы процесса и в отчёты об ошибках.
/// `scripts/check_secrets.py` роняет сборку на `getenv` в коде.
class SecdistSecretSource final : public application::ports::SecretSource {
public:
    static constexpr std::string_view kSection = "pdr";

    explicit SecdistSecretSource(const userver::components::Secdist& secdist) noexcept;

    std::optional<core::SecretString> Find(std::string_view name) const override;

private:
    const userver::components::Secdist& secdist_;
};

}  // namespace pdr::infrastructure
