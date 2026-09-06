#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/testsuite/tasks.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/utils/statistics/entry.hpp>
#include <userver/utils/statistics/rate_counter.hpp>
#include <userver/utils/statistics/writer.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/db/unscoped_access.hpp"
#include "infrastructure/userver_clock.hpp"
#include "notifications/application/dispatch_outbox.hpp"
#include "notifications/infrastructure/dynamic_config_retry_policies.hpp"
#include "notifications/infrastructure/logging_notification_sender.hpp"
#include "notifications/infrastructure/postgres_outbox_queue.hpp"

namespace pdr::notifications {

/// ОТПРАВЩИК: разбирает очередь на КАЖДОМ процессе кластера.
///
/// Здесь нет распределённой блокировки, и это не забывчивость. Одиночные
/// задания (`jobs::PeriodicJobComponentBase`, ADR-0011) — про работу, единицей
/// которой является «наступило время»: брать нечего, пока кто-то не решит, что
/// пора, и если решат двое, работа будет сделана дважды. Очередь — другой вид
/// работы и прямо противоположный: единиц много, они лежат строками, и чем
/// больше воркеров, тем быстрее она разбирается. Блокировка здесь оставила бы
/// одного работника из трёх и ничего не дала бы взамен.
///
/// Двое не отправят одно письмо дважды не потому, что мы им не дали, а потому,
/// что вторая реплика не получит захваченную строку: `for update skip locked`
/// решает это в базе (ADR-0002), а не в процессе. Мьютекс в одном процессе про
/// второй процесс не знает вовсе.
///
/// ПОД КОНТУРОМ ПРОХОД ИДЁТ ПО ТРЕБОВАНИЮ, А НЕ ПО ЧАСАМ. Отправщик, крутящийся
/// сам по себе, разберёт очередь ПОСРЕДИ проверки — и тест «падение между
/// записью и отправкой ничего не теряет» станет зависеть от того, кто успел
/// первым. Поэтому под набором периодическое занятие не заводится, а проход
/// зовёт сам набор.
class OutboxDispatcher final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "notifications-outbox-dispatcher";

    OutboxDispatcher(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

    ~OutboxDispatcher() override;

    void OnAllComponentsLoaded() override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    /// ЧТО НАКОПИЛОСЬ ЗА ВСЕ ПРОХОДЫ, а не что было в последнем. Типы штатные:
    /// `Rate` — счётчик, читатель метрик знает, что их складывают и что они не
    /// убывают.
    struct Passes final {
        userver::utils::statistics::RateCounter claimed;
        userver::utils::statistics::RateCounter sent;
        userver::utils::statistics::RateCounter retried;
        userver::utils::statistics::RateCounter gave_up;
        userver::utils::statistics::RateCounter refused;
    };

    void Pass();

    void DumpMetrics(userver::utils::statistics::Writer& writer) const;

    infrastructure::UserverClock clock_;
    DynamicConfigRetryPolicies settings_;
    infrastructure::db::UnscopedAccess unscoped_;
    PostgresOutboxQueue queue_;
    LoggingNotificationSender sender_;
    DispatchOutbox dispatching_;
    userver::testsuite::TestsuiteTasks& tasks_;
    userver::utils::PeriodicTask pass_;
    Passes passes_;
    userver::utils::statistics::Entry statistics_;
};

}  // namespace pdr::notifications
