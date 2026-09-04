#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <userver/clients/http/client.hpp>
#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/utils/retry_budget.hpp>
#include <userver/utils/statistics/entry.hpp>
#include <userver/utils/token_bucket.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/http/outgoing.hpp"
#include "infrastructure/observe/service_alerts.hpp"

namespace pdr::infrastructure::http {

/// Что не так со сроками направлений при таком сроке входящего запроса.
///
/// Пусто — настройка годится. Иначе это текст отказа: он поедет в исключение
/// конструктора компонента, то есть в причину, по которой процесс не поднялся.
///
/// Отдельной функцией, а не строкой внутри конструктора, ровно затем, чтобы
/// правило можно было проверить без поднятого процесса.
std::optional<std::string> WhatIsWrongWithDeadlines(
    const std::unordered_map<std::string, std::chrono::milliseconds>& directions,
    std::chrono::milliseconds deadline);

/// ЕДИНСТВЕННАЯ ДВЕРЬ НАРУЖУ.
///
/// Чужой HTTP ходит только штатным клиентом userver, и только отсюда: здесь
/// направлению достаются его срок, его бюджет повторов и его квота. Синхронный
/// клиент в корутинном рантайме блокирует поток целиком, поэтому libcurl, cpr и
/// подобное запрещены и ловятся scripts/check_handmade.py.
///
/// ОТКАЗ ПОДНИМАТЬСЯ при негодной настройке, а не запись в журнал. Исходящий
/// срок, который не меньше срока входящего запроса (PDR_REQUEST_DEADLINE),
/// означает вызов, переживающий собственный запрос: соединение занято, а ответа
/// уже никто не ждёт. Такую настройку нельзя оставить действующей — процесс с
/// ней не начинает слушать порт.
///
/// СОСТАВ НАПРАВЛЕНИЙ БЕРЁТСЯ НА СТАРТЕ, ЧИСЛА — ЖИВЫЕ. Появившееся в конфиге
/// направление подхватывается перезапуском: у направления есть своя метрика и
/// свой бюджет, и заводить их посреди обслуживания значило бы менять состав
/// метрик под ногами у того, кто на них смотрит. А срок, число обращений, бюджет
/// и квота у заведённого направления меняются на живом процессе.
///
/// НЕГОДНОЕ ИЗМЕНЕНИЕ ОТВЕРГАЕТСЯ ЦЕЛИКОМ, а не применяется наполовину: набор
/// сроков, не проходящий ту же проверку, что и на старте, не применяется вовсе,
/// и в журнал уходит отказ. Так свойство «исходящий срок меньше входящего»
/// держится всё время работы, а не только в первую секунду.
class OutgoingCallsComponent final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "outgoing-calls";

    OutgoingCallsComponent(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context);
    ~OutgoingCallsComponent() override;

    /// Направление по имени. Неизвестное имя — ошибка настройки, и она бросает:
    /// молчаливое «направления нет» превратилось бы в тихо неработающую функцию.
    Outgoing For(std::string_view direction) const;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    struct Direction final {
        Direction(std::chrono::milliseconds timeout,
                  int attempts,
                  const userver::utils::RetryBudgetSettings& budget,
                  userver::utils::TokenBucket quota);

        std::atomic<std::int64_t> timeout_ms;
        std::atomic<int> attempts;
        userver::utils::RetryBudget budget;
        userver::utils::TokenBucket quota;
    };

    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);
    void DumpMetrics(userver::utils::statistics::Writer& writer) const;

    userver::clients::http::Client& client_;
    observe::ServiceAlerts alerts_;
    std::unordered_map<std::string, std::unique_ptr<Direction>> directions_;
    userver::utils::statistics::Entry statistics_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::infrastructure::http
