#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <userver/utils/statistics/metrics_storage.hpp>

namespace pdr::infrastructure::observe {

/// ЧТО СЕРВИС ЗНАЕТ О СЕБЕ ТОЧНЕЕ, ЧЕМ ПРАВИЛО СНАРУЖИ.
///
/// PDR-OBS-02 строит тревоги на правилах Prometheus, и для большинства условий
/// это верно: «пятисотки выросли» видно снаружи и не видно изнутри. Но часть
/// условий снаружи выводится косвенно, а изнутри известна прямо — и разница
/// между «выведено» и «известно» это разница между догадкой дежурного и фактом.
///
/// Список закрытый: сигнал, который сервис поднимает, но не описал в
/// docs/architecture/observability.md, — это красный огонёк без инструкции.
/// Сверяет `scripts/check_log_fields.py`.
enum class ServiceAlert : std::uint8_t {
    /// Схема пуста: миграции не применены. Снаружи это «readiness отвечает 503»
    /// — то же самое, что при недоступной базе, при незанятом порте и при
    /// неподнявшемся процессе. Изнутри это ровно одна причина из четырёх.
    kMigrationsNotApplied,

    /// Хранилище не отвечает на запрос готовности.
    kStorageUnreachable,

    /// Доставленный PDR_OUTGOING_CALLS отвергнут целиком: направления работают
    /// по прежним числам. Снаружи не видно НИЧЕГО — метрики не меняются именно
    /// потому, что настройка не применилась.
    kOutgoingCallsRefused,

    /// Задание молчит дольше разрешённого. Изнутри известен и последний прогон,
    /// и разрешённая тишина из динамического конфига; снаружи разрешённую
    /// тишину пришлось бы дублировать в правило и потом помнить про оба места.
    kJobHasFallenSilent,

    /// ГРАНИЦА СПИСКА, а не сигнал.
    kBoundary,
};

/// Машинное имя сигнала: то же слово, что в docs/architecture/observability.md
/// и в метрике `alerts.<имя>`.
std::string_view Name(ServiceAlert alert) noexcept;

inline constexpr std::array<ServiceAlert, 4> kEveryServiceAlert{
    ServiceAlert::kMigrationsNotApplied,
    ServiceAlert::kStorageUnreachable,
    ServiceAlert::kOutgoingCallsRefused,
    ServiceAlert::kJobHasFallenSilent,
};

static_assert(kEveryServiceAlert.size() == static_cast<std::size_t>(ServiceAlert::kBoundary),
              "сигнал заведён, а в kEveryServiceAlert его нет: он не поднимется ни разу, и "
              "заметить это будет нечем");

/// Поднять и снять сигнал штатным `alerts::Source`.
///
/// Своего механизма здесь нет ни строчки: `alerts::Source` — это
/// `utils::statistics::MetricTag`, то есть та же метрика на служебном порту, и
/// формат отдачи (в том числе прометеевский) — дело `server-monitor`.
/// prometheus-cpp запрещён ADR-0013.
///
/// СИГНАЛ ГАСНЕТ САМ. `FireAlert` поднимает его на срок, и не подтверждённый
/// заново он опускается: висящий вечно сигнал перестают замечать на второй
/// день. Поэтому условие проверяется периодически, а не однажды.
class ServiceAlerts final {
public:
    /// Срок по умолчанию у штатного механизма — две минуты. Здесь он назван
    /// параметром: условие, проверяемое раз в минуту, и условие, проверяемое
    /// раз в час, гаснут по-разному.
    static constexpr std::chrono::seconds kDefaultDuration{120};

    explicit ServiceAlerts(userver::utils::statistics::MetricsStorage& storage) noexcept;

    void Raise(ServiceAlert alert, std::chrono::seconds duration = kDefaultDuration) const;

    void Clear(ServiceAlert alert) const;

private:
    userver::utils::statistics::MetricsStorage& storage_;
};

}  // namespace pdr::infrastructure::observe
