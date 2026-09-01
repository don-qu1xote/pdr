#pragma once

#include <optional>
#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "infrastructure/secdist/secdist_secret_source.hpp"

namespace pdr::scheduling_service {

/// ОТКАЗ ПОДНИМАТЬСЯ БЕЗ СЕКРЕТА, а не запись в журнал.
///
/// Проверка написана в PDR-SEC-02, а звать её было некому: точки входа не
/// существовало. Здесь она зовётся — в конструкторе компонента, то есть до
/// того, как сервис начнёт слушать порт.
///
/// Предупреждение в журнале означало бы, что процесс поднялся и работает «как
/// будто нормально»: он жив, метрика зелёная, а подключение к базе не
/// установилось. Узнаёт об этом не мониторинг, а человек.
class SecretsGuard final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "secrets-guard";

    SecretsGuard(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

private:
    std::optional<infrastructure::SecdistSecretSource> source_;
};

}  // namespace pdr::scheduling_service
