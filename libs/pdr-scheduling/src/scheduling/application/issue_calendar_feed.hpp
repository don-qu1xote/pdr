#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/digests.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/calendar_feeds.hpp"
#include "scheduling/core/calendar_feed.hpp"

namespace pdr::scheduling {

/// Сценарий: выдать ссылку на календарь — и он же перевыпустить её.
///
/// ОДНО ДЕЙСТВИЕ НА ОБА СЛУЧАЯ. Ссылка утекла — человек нажимает ту же кнопку,
/// что и в первый раз, и старая перестаёт работать немедленно. Отдельный
/// «отзыв» оставил бы промежуток, в котором ленты нет ни старой, ни новой, и
/// вопрос «а он уже перевыпустил?» без ответа.
///
/// СЕКРЕТ ОТДАЁТСЯ РОВНО ОДИН РАЗ — здесь. В базу уезжает отпечаток, и второй
/// раз показать ссылку неоткуда: это не забывчивость, а то самое свойство,
/// ради которого отпечаток и хранится.
///
/// Генератор — `SecretGenerator`, а не `IdGenerator`, и подставить второй в
/// первый нельзя: это ошибка компиляции. Адрес ленты живёт в чужих настройках
/// годами, и предсказуемый адрес отдаёт чужое расписание молча.
class IssueCalendarFeed final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId person;
    };

    struct Answer final {
        CalendarFeed feed;

        /// Секрет ссылки — открытым текстом и единственный раз за всю его жизнь.
        CalendarFeedSecret secret;
    };

    /// Сколько байт случайности в секрете ссылки. Тридцать два: ссылка живёт в
    /// чужих настройках годами, и перебирать её будут не человеком, а машиной.
    static constexpr std::size_t kSecretBytes = 32;

    IssueCalendarFeed(ports::CalendarFeeds& feeds,
                      const application::ports::SecretGenerator& secrets,
                      const application::ports::Digests& digests,
                      const application::ports::Clock& clock) noexcept;

    core::Result<Answer> Execute(const Request& request) const;

private:
    ports::CalendarFeeds& feeds_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Digests& digests_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::scheduling
