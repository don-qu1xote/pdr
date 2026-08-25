#pragma once

#include <string>
#include <unordered_map>

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/to.hpp>

#include "core/errors.hpp"
#include "jobs/application/ports/job_settings_source.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/job_settings.hpp"

namespace pdr::jobs {

/// Словарь заданий: ключ — имя компонента задания.
using PeriodicJobs = std::unordered_map<std::string, JobSettings>;

/// Ключ переменной — ОДИН объект на всё дерево. Второй `dynamic_config::Key` с
/// тем же именем даёт вторую ячейку хранилища: подмена значения в тесте тогда
/// не доходит до адаптера, а выглядит это как «конфиг не применился».
extern const userver::dynamic_config::Key<PeriodicJobs> kPeriodicJobs;

/// Разбор одной записи `PDR_PERIODIC_JOBS`. Значения — целые миллисекунды:
/// «1h» строкой в динамическом конфиге читается по-разному в разных местах, а
/// число не читается никак иначе.
///
/// Найдено по ADL из `pdr::jobs`, как того требует userver.
JobSettings Parse(const userver::formats::json::Value& value,
                  userver::formats::parse::To<JobSettings>);

/// Имя, период, отведённое на прогон время и срок молчания — из динамического
/// конфига (`PDR-CFG-01`), а не из констант.
///
/// Умолчание есть только у переменной целиком — пустой словарь. У отдельного
/// задания умолчаний нет: задания, которого нет в конфиге, для механизма не
/// существует, и это отказ, а не «возьмём минуту и поедем». Забытая настройка
/// обязана выглядеть как поломка, а не как работающее задание.
class DynamicConfigJobSettings final : public ports::JobSettingsSource {
public:
    /// Ключ переменной. Публичный намеренно: сервису его же прописывать в
    /// обновлятор конфигов, и второй строки с этим именем в дереве быть не должно.
    static constexpr std::string_view kVariable = "PDR_PERIODIC_JOBS";

    explicit DynamicConfigJobSettings(userver::dynamic_config::Source source);

    ~DynamicConfigJobSettings() override;

    core::Result<JobSettings> For(const JobName& job) const override;

private:
    /// Журнал изменений: что стало с каждым заданием при очередном применении
    /// конфига. Подписка штатная — `dynamic_config::Diff` приносит предыдущий
    /// снимок вместе с текущим.
    ///
    /// «Кто изменил» здесь взять негде: подписка знает «когда» и «с какого на
    /// какое», а авторство приносит источник конфигов, которого в дереве пока
    /// нет (docs/architecture/first-service.md).
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::jobs
