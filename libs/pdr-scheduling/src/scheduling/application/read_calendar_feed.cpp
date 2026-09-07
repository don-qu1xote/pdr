#include "scheduling/application/read_calendar_feed.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "scheduling/core/lesson_state.hpp"
#include "scheduling/core/recurrence.hpp"

namespace pdr::scheduling {
namespace {

/// Опознаватель события, устойчивый между обновлениями ленты.
///
/// Календарь узнаёт по нему уже показанное событие: сменившийся опознаватель —
/// это удалённое событие и заведённое заново, с потерянными напоминаниями и
/// потерянным местом в чужом расписании. Поэтому он собирается из
/// идентификатора, который не меняется, а не из времени или из порядка.
std::string Uid(std::string_view kind, const std::string& id) {
    return std::string{kind} + "-" + id + "@pdr";
}

/// Занятие, которое календарю показывать. Отменённое — не показывать: человек
/// увидит в своём календаре занятие, которого не будет.
bool Shown(const Lesson& lesson) noexcept {
    return lesson.State() != LessonState::kCancelled;
}

/// Момент вхождения серии: местная дата плюс час серии по правилам её зоны.
///
/// `Resolve` возвращает три ответа, и здесь берётся первый из них при любом:
/// пропавший час весной даёт конец пропавшего промежутка, повторившийся осенью
/// — первое из двух. Спорить с этим в ленте нечем — календарь получит момент, и
/// момент правильный; показать человеку странность может только сам продукт.
core::Instant MomentOf(const RecurrenceSeries& series,
                       const core::Date& date,
                       const core::ZoneOffsets& offsets) {
    return core::Resolve(core::LocalDateTime{date, series.At()}, offsets).first;
}

/// Первая дата, на которую правило ставит вхождение. Пусто у серии, которая не
/// ставит ни одного, — такая в ленту не попадает вовсе.
///
/// Отрезок поиска — интервал правила в неделях плюс запас на одну: дальше
/// первого вхождения искать нечего, а правило с `INTERVAL=12` ставит его не
/// раньше двенадцатой недели.
std::optional<core::Date> FirstOccurrence(const RecurrenceSeries& series) {
    const auto within = std::chrono::days{7 * (series.Rule().Interval() + 1)};
    const std::chrono::year_month_day start{
        std::chrono::year{series.StartsOn().Year()},
        std::chrono::month{series.StartsOn().Month()},
        std::chrono::day{series.StartsOn().Day()},
    };
    const std::chrono::year_month_day until{std::chrono::sys_days{start} + within};

    const auto dates = OccurrenceDates(series,
                                       series.StartsOn(),
                                       core::Date::Compose(static_cast<int>(until.year()),
                                                           static_cast<unsigned>(until.month()),
                                                           static_cast<unsigned>(until.day()))
                                           .Value());
    if (dates.empty()) {
        return std::nullopt;
    }
    return dates.front();
}

/// НАЧАЛО ОКНА ОБНОВЛЕНИЯ, а не «сейчас».
///
/// `DTSTAMP` обязателен у каждого события, и «сейчас» в нём меняло бы ленту на
/// каждом обращении — вместе с её отпечатком. Тогда условный запрос календаря
/// не сработал бы никогда, а он и есть весь смысл кэширования: календари ходят
/// сюда часто и без спроса.
///
/// Округление до окна обновления оставляет ленту неизменной, пока не изменилось
/// расписание, и меняет её не чаще раза в это самое окно.
core::Instant WindowOf(core::Instant now) {
    const auto window =
        std::chrono::duration_cast<core::Instant::Duration>(std::chrono::minutes{kRefreshMinutes})
            .count();
    return core::Instant::FromUnixMicros(now.UnixMicros() / window * window);
}

}  // namespace

ReadCalendarFeed::ReadCalendarFeed(const ports::CalendarFeeds& feeds,
                                   const ports::LessonRepository& lessons,
                                   const ports::RecurrenceRepository& series,
                                   const application::ports::TimeZoneRules& zones,
                                   const application::ports::Digests& digests,
                                   const application::ports::Clock& clock,
                                   CalendarWording wording) noexcept
    : feeds_{feeds},
      lessons_{lessons},
      series_{series},
      zones_{zones},
      digests_{digests},
      clock_{clock},
      wording_{std::move(wording)} {}

core::Result<ReadCalendarFeed::Answer> ReadCalendarFeed::Execute(const Request& request) const {
    const auto naming = feeds_.NamingOf(request.tenant, request.person);
    if (!naming.has_value()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "calendar_feed_not_found",
                           "такой подписки на календарь здесь нет"};
    }

    const auto now = clock_.Now();
    const auto horizon = FeedHorizon(now);

    CalendarFeedContents contents{wording_.name, WindowOf(now), {}, {}};

    /// Правила зоны спрашиваются ОДИН РАЗ НА ЗОНУ и только для горизонта ленты:
    /// переводов часов у зоны за всю её историю сотни, а в ленту уезжают те,
    /// что попадают в её отрезок.
    const auto rules = [&](const core::TimeZone& zone) -> core::Result<void> {
        for (const auto& [named, offsets] : contents.zones) {
            if (named == zone) {
                return {};
            }
        }
        const auto known = zones_.For(zone, horizon);
        if (!known.HasValue()) {
            return known.Failure();
        }
        contents.zones.emplace_back(zone, known.Value());
        return {};
    };

    const auto offsets = [&](const core::TimeZone& zone) -> const core::ZoneOffsets& {
        for (const auto& [named, known] : contents.zones) {
            if (named == zone) {
                return known;
            }
        }
        return contents.zones.front().second;
    };

    /// ОДИНОЧНЫЕ ЗАНЯТИЯ. Спрашиваются обе стороны сразу — «занятия репетитора»
    /// и «занятия участника», — и развилки по роли здесь нет ни одной: у
    /// репетитора не найдётся занятий, где он ученик, и наоборот.
    auto found = lessons_.OfTutor(request.tenant, request.person, horizon);
    for (auto& lesson : lessons_.OfParticipant(request.tenant, request.person, horizon)) {
        const bool known = std::any_of(found.begin(), found.end(), [&](const Lesson& kept) {
            return kept.Id() == lesson.Id();
        });
        if (!known) {
            found.push_back(std::move(lesson));
        }
    }
    std::sort(found.begin(), found.end(), [](const Lesson& left, const Lesson& right) {
        return left.StartsAt() < right.StartsAt();
    });

    for (const auto& lesson : found) {
        if (!Shown(lesson)) {
            continue;
        }
        const auto named = rules(lesson.Zone());
        if (!named.HasValue()) {
            return named.Failure();
        }

        contents.entries.push_back(CalendarEntry{Uid("lesson", lesson.Id().ToString()),
                                                 lesson.StartsAt(),
                                                 lesson.LessonDuration(),
                                                 lesson.Zone(),
                                                 wording_.lesson,
                                                 std::string{},
                                                 std::string{},
                                                 {}});
    }

    /// СЕРИИ — ПРАВИЛОМ. Разворачивать их здесь было бы вторым генератором
    /// повторов рядом с тем, которым серия хранится.
    for (const auto& id : series_.Of(request.tenant, request.person)) {
        const auto found_series = series_.Find(request.tenant, id);
        if (!found_series.has_value()) {
            continue;
        }
        const auto named = rules(found_series->Zone());
        if (!named.HasValue()) {
            return named.Failure();
        }
        const auto& zone_rules = offsets(found_series->Zone());

        /// ПЕРВОЕ ВХОЖДЕНИЕ ЗАДАЁТ DTSTART СЕРИИ, и это не то же самое, что дата
        /// её начала: серия «по вторникам с субботы» начинается во вторник.
        /// Ищется оно тем же обходом, которым серия разворачивается всюду
        /// (PDR-SCHED-02), и на отрезке в интервал правила: дальше первого
        /// вхождения смотреть незачем, а раньше его — не с чего.
        const auto opening = FirstOccurrence(*found_series);
        if (!opening.has_value()) {
            continue;
        }

        CalendarEntry entry{Uid("series", id.ToString()),
                            MomentOf(*found_series, *opening, zone_rules),
                            found_series->LessonDuration(),
                            found_series->Zone(),
                            wording_.lesson,
                            std::string{},
                            found_series->Rule().ToRRule(),
                            {}};

        for (const auto& exception : found_series->Exceptions()) {
            entry.cancelled.push_back(MomentOf(*found_series, exception.occurrence_on, zone_rules));

            /// ПЕРЕНЕСЁННОЕ ВХОЖДЕНИЕ — та же EXDATE и отдельное событие на
            /// новом месте: из правила оно ушло, но состояться должно.
            if (exception.kind == ExceptionKind::kMoved && exception.moved_to.has_value()) {
                contents.entries.push_back(CalendarEntry{
                    Uid("moved",
                        id.ToString() + "-" + std::to_string(exception.occurrence_on.Year()) +
                            std::to_string(exception.occurrence_on.Month()) +
                            std::to_string(exception.occurrence_on.Day())),
                    *exception.moved_to,
                    exception.moved_duration.has_value() ? *exception.moved_duration
                                                         : found_series->LessonDuration(),
                    found_series->Zone(),
                    wording_.lesson,
                    std::string{},
                    std::string{},
                    {}});
            }
        }

        contents.entries.push_back(std::move(entry));
    }

    const auto built = BuildCalendar(contents);
    if (!built.HasValue()) {
        return built.Failure();
    }

    /// ОТПЕЧАТОК СОДЕРЖИМОГО, А НЕ ВРЕМЯ СБОРКИ. Календарь ходит за лентой раз
    /// в час и чаще; неизменившуюся ленту отдавать целиком незачем, а «время
    /// сборки» менялось бы на каждом обращении и не совпало бы никогда.
    return Answer{built.Value(), digests_.Of(built.Value()).Value()};
}

}  // namespace pdr::scheduling
