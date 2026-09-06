#include "scheduling/core/booking_window.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace pdr::scheduling {
namespace {

using Notice = BookingWindows::Notice;

core::Error Negative(std::string_view window) {
    return core::Error{core::ErrorKind::kValidation,
                       "booking_window_negative",
                       "окно «" + std::string{window} + "» отсчитано назад: так не бывает"};
}

core::Error Tightened(std::string_view window) {
    return core::Error{
        core::ErrorKind::kValidation,
        "booking_window_tightened",
        "окно «" + std::string{window} +
            "» для этого ученика строже общего: послабление даётся, а не отнимается"};
}

bool Whole(const std::optional<Notice>& window) noexcept {
    return !window.has_value() || *window >= Notice::zero();
}

/// Слабее ли срок предупреждения. Меньший срок разрешает больше; пустота
/// разрешает всё, поэтому слабее любого срока и не слабее только самой себя.
bool NoticeIsWeaker(const std::optional<Notice>& base,
                    const std::optional<Notice>& relief) noexcept {
    if (!base.has_value()) {
        return !relief.has_value();
    }
    return !relief.has_value() || *relief <= *base;
}

/// Слабее ли горизонт. Здесь наоборот: дальше видно — больше разрешено.
bool HorizonIsWeaker(const std::optional<Notice>& base,
                     const std::optional<Notice>& relief) noexcept {
    if (!base.has_value()) {
        return !relief.has_value();
    }
    return !relief.has_value() || *relief >= *base;
}

std::optional<Notice> NoticeFor(const BookingWindows& windows, BookingAction action) noexcept {
    switch (action) {
        case BookingAction::kBook:
            return windows.MinNoticeBook();
        case BookingAction::kReschedule:
            return windows.MinNoticeReschedule();
        case BookingAction::kCancel:
            return windows.MinNoticeCancel();
        case BookingAction::kBoundary:
            break;
    }
    return std::nullopt;
}

core::Error TooLate(BookingAction action) {
    switch (action) {
        case BookingAction::kBook:
            return core::Error{core::ErrorKind::kConflict,
                               "booking_too_late",
                               "до начала осталось меньше, чем репетитор просит на подготовку"};
        case BookingAction::kReschedule:
            return core::Error{core::ErrorKind::kConflict,
                               "reschedule_too_late",
                               "переносить так близко к началу уже поздно"};
        case BookingAction::kCancel:
            return core::Error{core::ErrorKind::kConflict,
                               "cancel_too_late",
                               "отменять так близко к началу уже поздно"};
        case BookingAction::kBoundary:
            break;
    }
    return core::Error{core::ErrorKind::kConflict, "booking_too_late", "поздно"};
}

}  // namespace

std::string_view Name(BookingAction action) noexcept {
    switch (action) {
        case BookingAction::kBook:
            return "book";
        case BookingAction::kReschedule:
            return "reschedule";
        case BookingAction::kCancel:
            return "cancel";
        case BookingAction::kBoundary:
            break;
    }
    return "book";
}

BookingWindows BookingWindows::Anything() noexcept {
    return BookingWindows{std::nullopt, std::nullopt, std::nullopt, std::nullopt};
}

core::Result<BookingWindows> BookingWindows::Compose(std::optional<Notice> book,
                                                     std::optional<Notice> horizon,
                                                     std::optional<Notice> reschedule,
                                                     std::optional<Notice> cancel) {
    if (!Whole(book)) {
        return Negative("запись");
    }
    if (!Whole(horizon)) {
        return Negative("горизонт");
    }
    if (!Whole(reschedule)) {
        return Negative("перенос");
    }
    if (!Whole(cancel)) {
        return Negative("отмена");
    }

    return BookingWindows{book, horizon, reschedule, cancel};
}

core::Result<void> Allows(const BookingWindows& windows,
                          BookingAction action,
                          BookingSide side,
                          core::Instant starts_at,
                          core::Instant now) {
    if (side == BookingSide::kScheduleOwner) {
        return {};
    }

    const auto ahead = starts_at - now;

    const auto notice = NoticeFor(windows, action);
    if (notice.has_value() && ahead < *notice) {
        return TooLate(action);
    }

    const auto horizon = windows.MaxHorizonBook();
    if (action == BookingAction::kBook && horizon.has_value() && ahead > *horizon) {
        return core::Error{core::ErrorKind::kConflict,
                           "booking_too_far",
                           "так далеко вперёд репетитор ещё не открыл своё расписание"};
    }

    return {};
}

core::Result<BookingWindows> Relax(const BookingWindows& base,
                                   const std::optional<BookingWindows>& relief) {
    if (!relief.has_value()) {
        return base;
    }

    if (!NoticeIsWeaker(base.MinNoticeBook(), relief->MinNoticeBook())) {
        return Tightened("запись");
    }
    if (!HorizonIsWeaker(base.MaxHorizonBook(), relief->MaxHorizonBook())) {
        return Tightened("горизонт");
    }
    if (!NoticeIsWeaker(base.MinNoticeReschedule(), relief->MinNoticeReschedule())) {
        return Tightened("перенос");
    }
    if (!NoticeIsWeaker(base.MinNoticeCancel(), relief->MinNoticeCancel())) {
        return Tightened("отмена");
    }

    return *relief;
}

std::vector<core::Instant> Offered(std::span<const core::Instant> slots,
                                   const BookingWindows& windows,
                                   BookingSide side,
                                   core::Instant now) {
    std::vector<core::Instant> offered;
    offered.reserve(slots.size());
    std::copy_if(slots.begin(), slots.end(), std::back_inserter(offered), [&](core::Instant slot) {
        return Allows(windows, BookingAction::kBook, side, slot, now).HasValue();
    });
    return offered;
}

}  // namespace pdr::scheduling
