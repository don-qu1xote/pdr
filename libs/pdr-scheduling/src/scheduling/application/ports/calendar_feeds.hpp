#pragma once

#include <optional>

#include "core/digest.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/calendar_feed.hpp"

namespace pdr::scheduling::ports {

/// Подписки на календарь: выдать, найти по предъявленному секрету, поправить
/// настройку.
///
/// ВЫДАЧА И ПЕРЕВЫПУСК — ОДИН МЕТОД, и это не экономия. Отзыв со своим методом
/// означал бы два состояния — «отозвана» и «выдана заново», — между которыми
/// человек остаётся без ленты, а мы с вопросом «а он уже перевыпустил?».
/// Утёкшую ссылку чинят одним нажатием: новый отпечаток поверх старого.
class CalendarFeeds {
public:
    CalendarFeeds(const CalendarFeeds&) = delete;
    CalendarFeeds& operator=(const CalendarFeeds&) = delete;

    virtual ~CalendarFeeds() = default;

    virtual core::Result<void> Issue(const CalendarFeed& feed, core::Instant issued_at) = 0;

    /// Подписка по отпечатку предъявленного секрета. Пусто — ссылка не наша или
    /// уже перевыпущена; для календаря это один и тот же ответ, и правильно.
    virtual std::optional<CalendarFeed> ByDigest(const core::TenantId& tenant,
                                                 const core::Digest& digest) const = 0;

    /// Способ называть занятия у этого человека — и заодно ответ, есть ли у
    /// него подписка вообще.
    ///
    /// ОТДАЁТСЯ НЕ ПОДПИСКА ЦЕЛИКОМ, А ТОЛЬКО НАСТРОЙКА, и это не экономия
    /// строк. В подписке лежит отпечаток секрета; тот, кто ленту уже отдаёт,
    /// пришёл опознанным, и отпечаток ему не нужен ни для чего. Отдать его
    /// значило бы однажды записать его в журнал.
    virtual std::optional<CalendarNaming> NamingOf(const core::TenantId& tenant,
                                                   const core::PersonId& person) const = 0;

    /// Поправить способ называть занятия. ОТДЕЛЬНО от секрета: настройка — не
    /// починка утечки, и менять ссылку тому, кто пришёл за настройкой, нельзя.
    virtual core::Result<void> Rename(const core::TenantId& tenant,
                                      const core::PersonId& person,
                                      CalendarNaming naming) = 0;

protected:
    CalendarFeeds() = default;
};

}  // namespace pdr::scheduling::ports
