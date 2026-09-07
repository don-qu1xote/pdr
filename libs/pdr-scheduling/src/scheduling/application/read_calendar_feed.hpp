#pragma once

#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/digests.hpp"
#include "application/ports/time_zone_rules.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/calendar_feeds.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"
#include "scheduling/core/calendar_feed.hpp"
#include "scheduling/core/icalendar.hpp"

namespace pdr::scheduling {

/// Слова, которыми лента называет саму себя и занятия.
///
/// ПРИХОДЯТ ДОВОДОМ, А НЕ ЖИВУТ В КОДЕ. Это первый в дереве текст, который
/// человек читает БЕЗ клиента: ленту разбирает его календарь, и перевести
/// что-либо на язык интерфейса по дороге некому. Литерал в C++ означал бы, что
/// поправить слово можно только выкаткой, — поэтому слова лежат в динамическом
/// конфиге (`PDR_CALENDAR_FEED`), а сюда приходят готовыми.
struct CalendarWording final {
    /// Имя календаря в списке подписок.
    std::string name;

    /// Название события, когда имён не показываем.
    std::string lesson;

    /// Название события с именем. `{name}` внутри заменяется на имя человека.
    std::string lesson_with_name;
};

/// Сценарий: отдать ленту по предъявленному секрету.
///
/// ЧИТАЮЩИЙ И НИЧЕГО НЕ МЕНЯЮЩИЙ — календарь ходит сюда сам, часто и без
/// спроса, и обращение, которое пишет, здесь было бы записью каждые несколько
/// часов на каждого подписчика.
///
/// СЕРИЯ УЕЗЖАЕТ ПРАВИЛОМ, А НЕ СПИСКОМ. `RecurrenceRule::ToRRule()` отдаёт ту
/// же строку RRULE, которой серия и хранится (PDR-SCHED-02); разворачивает её
/// календарь. Второго генератора повторов в дереве нет — а он бы завёлся ровно
/// здесь, если бы ленту собирали развёрткой.
///
/// ОТМЕНЁННОЕ ВХОЖДЕНИЕ УХОДИТ СТРОКОЙ EXDATE, перенесённое — той же EXDATE и
/// отдельным событием на новом месте. Так это и описано стандартом, и так это
/// понимают все четыре календаря, ради которых всё затевалось.
class ReadCalendarFeed final {
public:
    struct Request final {
        core::TenantId tenant;

        /// Кому лента. Приходит уже опознанным: секрет ссылки превратил в
        /// человека тот, кто опознаёт пришедших (`FeedCallers`).
        core::PersonId person;
    };

    struct Answer final {
        /// Текст ленты по RFC 5545.
        std::string calendar;

        /// Отпечаток содержимого для условного запроса. Календарь приходит
        /// часто; неизменившуюся ленту дешевле не отдавать вовсе.
        std::string etag;
    };

    ReadCalendarFeed(const ports::CalendarFeeds& feeds,
                     const ports::LessonRepository& lessons,
                     const ports::RecurrenceRepository& series,
                     const application::ports::TimeZoneRules& zones,
                     const application::ports::Digests& digests,
                     const application::ports::Clock& clock,
                     CalendarWording wording) noexcept;

    core::Result<Answer> Execute(const Request& request) const;

private:
    const ports::CalendarFeeds& feeds_;
    const ports::LessonRepository& lessons_;
    const ports::RecurrenceRepository& series_;
    const application::ports::TimeZoneRules& zones_;
    const application::ports::Digests& digests_;
    const application::ports::Clock& clock_;
    CalendarWording wording_;
};

}  // namespace pdr::scheduling
