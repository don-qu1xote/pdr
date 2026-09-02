#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "core/errors.hpp"
#include "jobs/application/ports/job_settings_source.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/job_settings.hpp"

namespace pdr::jobs {

/// Имя, период, отведённое на прогон время и срок молчания — из динамического
/// конфига (`PDR-CFG-01`), а не из констант.
///
/// НИ КЛЮЧА, НИ РАЗБОРА ЗДЕСЬ НЕТ. Словарь заданий порождён из
/// `PDR_PERIODIC_JOBS` в configs/dynamic/registry.yaml вместе с умолчанием и
/// пределами каждого поля; значением словаря сразу стоит доменный `JobSettings`
/// — так велит `x-usrv-cpp-type` в схеме, а превращает одно в другое
/// `Convert` рядом с типом (jobs/infrastructure/chaotic-io/...).
///
/// СВЯЗЬ МЕЖДУ ПОЛЯМИ ОСТАЛАСЬ ДОМЕННОЙ. «Прогону отведено меньше периода» и
/// «молчание длиннее периода» схемой не выражаются — их проверяет
/// `JobSettings::Compose`, и негодная запись отвергает разбор ВСЕЙ величины:
/// прежние настройки продолжают действовать.
///
/// Умолчание есть только у переменной целиком — пустой словарь. У отдельного
/// задания умолчаний нет: задания, которого нет в конфиге, для механизма не
/// существует, и это отказ, а не «возьмём минуту и поедем». Забытая настройка
/// обязана выглядеть как поломка, а не как работающее задание.
class DynamicConfigJobSettings final : public ports::JobSettingsSource {
public:
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
