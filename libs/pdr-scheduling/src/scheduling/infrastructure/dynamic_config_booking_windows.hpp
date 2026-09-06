#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "scheduling/application/ports/booking_window_defaults.hpp"

namespace pdr::scheduling {

/// УМОЛЧАНИЯ ПЛОЩАДКИ из динамического конфига — те же для всех практик.
///
/// НИ ОДНОЙ ОБЯЗАТЕЛЬНОЙ НАСТРОЙКИ: адаптер отвечает годными окнами и тогда,
/// когда источник конфигов недоступен вовсе, — умолчания порождены из реестра и
/// лежат в коде. Экран «сначала задайте четыре срока» не появляется никогда.
///
/// ДВЕ ВЕЛИЧИНЫ ИЗ РАЗНЫХ ЗАПИСЕЙ, и это не случайность. Сроки предупреждения —
/// про вежливость к чужому времени (`PDR_BOOKING_WINDOWS`), горизонт — про то,
/// на сколько вперёд репетитор вообще открыл расписание (`PDR_SCHEDULE_HORIZON`),
/// и правят их по разным поводам. Второе место для горизонта разошлось бы с
/// первым в первый же день.
///
/// ОКНА БЕСПЛАТНОЙ ОТМЕНЫ ЗДЕСЬ НЕТ. Оно лежит в той же записи реестра, но
/// отвечает на другой вопрос — сколько стоит поздняя отмена, — и читает его
/// политика удержания (`DynamicConfigCancellationPolicies`). Смешать их значит
/// запретить то, за что достаточно взять деньги.
///
/// Тенант окна пока не переопределяет: таблицы настроек репетитора у расписания
/// нет. Порт при этом уже спрашивает про тенанта — появится таблица, поменяется
/// адаптер, а сценарии и домен не заметят.
class DynamicConfigBookingWindows final : public ports::BookingWindowDefaults {
public:
    explicit DynamicConfigBookingWindows(userver::dynamic_config::Source source);

    ~DynamicConfigBookingWindows() override;

    core::Result<BookingWindows> ForPractice(const core::TenantId& tenant) const override;

private:
    /// Журнал «было → стало». Окна правят редко и по серьёзному поводу; запись о
    /// смене — единственное, по чему потом восстановят, с какого дня у людей
    /// поменялись условия.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::scheduling
