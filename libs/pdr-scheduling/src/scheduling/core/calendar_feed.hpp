#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "core/digest.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/icalendar.hpp"

namespace pdr::scheduling {

/// СЕКРЕТ ССЫЛКИ НА ЛЕНТУ.
///
/// Ссылку на календарь человек вставляет в чужую программу и больше о ней не
/// думает: она живёт в настройках телефона годами и обновляется без спроса.
/// Поэтому она обязана быть НЕПРЕДСКАЗУЕМОЙ — угаданный адрес отдаёт чужое
/// расписание молча, и заметить это некому.
///
/// Не меньше 32 байт случайности, то есть 43 знаков base64url — тот же порог,
/// что и у одноразовой ссылки identity, и по той же причине.
class CalendarFeedSecret final {
public:
    static constexpr std::size_t kLeastLength = 43;

    static core::Result<CalendarFeedSecret> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

private:
    explicit CalendarFeedSecret(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

/// ПОДПИСКА НА КАЛЕНДАРЬ: чья она, чем открывается и как называет занятия.
///
/// В БАЗЕ САМОГО СЕКРЕТА НЕТ — там лежит только отпечаток. Утёкшая копия базы
/// не даёт работающих ссылок: из отпечатка секрет не восстанавливается, а
/// сравнить пришедшее с хранимым он позволяет.
///
/// ОДНА ПОДПИСКА НА ЧЕЛОВЕКА. Не потому, что двух не бывает, а потому, что
/// «отозвать ссылку» обязано быть ОДНИМ действием: со списком ссылок человек в
/// минуту утечки выбирает, какую из четырёх отозвать, — и выбирает не ту.
///
/// Перевыпуск — это та же строка с новым отпечатком: старая ссылка перестаёт
/// работать в тот же миг, потому что её отпечатка в базе больше нет.
class CalendarFeed final {
public:
    static core::Result<CalendarFeed> Compose(core::TenantId tenant,
                                              core::PersonId person,
                                              core::Digest secret,
                                              CalendarNaming naming);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Person() const noexcept {
        return person_;
    }
    const core::Digest& Secret() const noexcept {
        return secret_;
    }
    CalendarNaming Naming() const noexcept {
        return naming_;
    }

    friend bool operator==(const CalendarFeed&, const CalendarFeed&) = default;

private:
    CalendarFeed(core::TenantId tenant,
                 core::PersonId person,
                 core::Digest secret,
                 CalendarNaming naming) noexcept
        : tenant_{std::move(tenant)},
          person_{std::move(person)},
          secret_{std::move(secret)},
          naming_{naming} {}

    core::TenantId tenant_;
    core::PersonId person_;
    core::Digest secret_;
    CalendarNaming naming_{CalendarNaming::kWithoutNames};
};

/// ГОРИЗОНТ ЛЕНТЫ: сколько назад и сколько вперёд она показывает.
///
/// Назад — чтобы календарь не терял только что прошедшее занятие, о котором
/// спорят («вы не пришли»). Вперёд — чтобы подписка была полезной и при этом
/// не разворачивала серию до конца века.
///
/// Числа здесь, а не в динамическом конфиге, по той же причине, что и срок
/// обновления: лента кэшируется у календаря, и величина, меняющаяся на лету,
/// доедет до человека когда придётся.
inline constexpr int kFeedPastDays = 30;
inline constexpr int kFeedAheadDays = 180;

/// Отрезок, который лента показывает, от этого «сейчас».
core::TimeRange FeedHorizon(core::Instant now);

}  // namespace pdr::scheduling
