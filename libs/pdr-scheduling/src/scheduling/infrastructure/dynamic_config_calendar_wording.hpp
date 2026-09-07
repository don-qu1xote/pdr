#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "scheduling/application/read_calendar_feed.hpp"

namespace pdr::scheduling {

/// СЛОВА ЛЕНТЫ ИЗ ДИНАМИЧЕСКОГО КОНФИГА (`PDR_CALENDAR_FEED`).
///
/// Единственный текст в дереве, который человек читает БЕЗ клиента: ленту
/// разбирает его собственный календарь, и перевести что-либо на язык интерфейса
/// по дороге некому. В `clients/shared/i18n/ru.json` он поэтому не помещается —
/// туда ходит клиент, а здесь клиента нет вовсе.
///
/// Динамическим, а не константой: подписка живёт у человека в настройках
/// годами, и поправить в ней слово выкаткой — значит не поправить никогда.
class DynamicConfigCalendarWording final {
public:
    explicit DynamicConfigCalendarWording(userver::dynamic_config::Source source);

    ~DynamicConfigCalendarWording();

    CalendarWording Words() const;

private:
    /// Журнал «было → стало». Подписка живёт у человека в настройках годами:
    /// «у меня в календаре другое слово» разбирается по записи о смене, а не по
    /// памяти дежурного.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::scheduling
