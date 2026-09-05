#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Серия кончается ПОСЛЕ стольких занятий.
struct Count final {
    int times{};

    friend bool operator==(const Count&, const Count&) = default;
};

/// Серия кончается ЭТОЙ ДАТОЙ включительно, по местному календарю репетитора.
struct Until final {
    core::Date date;

    friend bool operator==(const Until&, const Until&) = default;
};

/// Чем серия кончается. ЛИБО СЧЁТ, ЛИБО ДАТА — третьего не бывает, и «ни того,
/// ни другого» тоже: бесконечная серия развернулась бы до конца горизонта у
/// каждого запроса, а обещание «до конца года» перестало бы быть выразимым.
using Ending = std::variant<Count, Until>;

/// ПРАВИЛО ПОВТОРЕНИЯ — ПОДМНОЖЕСТВО RRULE (RFC 5545), И ТОЛЬКО ОНО.
///
/// Поддержано ровно то, чем пользуется репетиторство:
///
///     FREQ=WEEKLY        — единственная частота. «Каждый вторник» и «раз в две
///                          недели по вторникам и четвергам» покрывают всё, что
///                          заказывают; месячные и годовые повторения занятий
///                          не бывают;
///     INTERVAL=N         — «раз в N недель», по умолчанию 1;
///     BYDAY=MO,TU,...    — дни недели, хотя бы один;
///     COUNT=N | UNTIL=…  — ровно одно из двух.
///
/// ВСЁ ОСТАЛЬНОЕ ОТКЛОНЯЕТСЯ ВСЛУХ, а не игнорируется. Разбор, молча
/// пропускающий `BYSETPOS`, отдаёт расписание, которого человек не просил, и
/// узнаёт он об этом на занятии, куда никто не пришёл. Поддержать весь стандарт
/// — это `BYSETPOS`, `WKST`, `BYMONTHDAY`, високосные февральские тридцатые и
/// свой набор тестов на каждое; репетитору из этого не нужно ничего.
class RecurrenceRule final {
public:
    static constexpr int kMaxInterval = 12;
    static constexpr int kMaxCount = 400;

    /// Собрать правило из значений. Дни недели упорядочиваются и не повторяются.
    static core::Result<RecurrenceRule> Compose(int interval,
                                                std::vector<core::Weekday> days,
                                                Ending ending);

    /// Разобрать строку RRULE. Части вне поддержанного подмножества — отказ с
    /// названным именем части, а не тихий пропуск.
    static core::Result<RecurrenceRule> Parse(std::string_view rrule);

    int Interval() const noexcept {
        return interval_;
    }
    const std::vector<core::Weekday>& Days() const noexcept {
        return days_;
    }
    const Ending& Ends() const noexcept {
        return ending_;
    }

    /// Обратно в строку RRULE — ту же, что разобралась бы в это правило.
    std::string ToRRule() const;

    friend bool operator==(const RecurrenceRule&, const RecurrenceRule&) = default;

private:
    RecurrenceRule(int interval, std::vector<core::Weekday> days, Ending ending) noexcept
        : interval_{interval}, days_{std::move(days)}, ending_{std::move(ending)} {}

    int interval_{1};
    std::vector<core::Weekday> days_;
    Ending ending_;
};

/// Что случилось с отдельным вхождением серии.
enum class ExceptionKind : std::uint8_t {
    /// Занятие отменено. Из развёртки исчезает совсем.
    kCancelled,

    /// Занятие перенесено. На расчётном месте его нет, на новом — есть.
    kMoved,

    /// ГРАНИЦА СПИСКА, а не вид исключения.
    kBoundary,
};

std::string_view Name(ExceptionKind kind) noexcept;

/// ИСКЛЮЧЕНИЕ ИЗ ПРАВИЛА — ДВУХ ВИДОВ, А НЕ ОДНОГО.
///
/// «Отменили» и «перенесли» — разные события и для расписания, и для оплаты, и
/// для ученика. Один вид исключения заставил бы изображать перенос парой
/// «отмена плюс новое занятие», и связь между ними держалась бы на памяти
/// того, кто её завёл.
///
/// Вхождение опознаётся МЕСТНОЙ ДАТОЙ, на которую его ставит правило. Не
/// моментом: момент у вхождения меняется при переводе часов, а дата — нет, и
/// «перенести занятие двадцать девятого» остаётся понятным и в ту ночь.
struct RecurrenceException final {
    core::Date occurrence_on;
    ExceptionKind kind{ExceptionKind::kCancelled};

    /// Куда перенесено. Обязательно у `kMoved` и пусто у `kCancelled`.
    std::optional<core::Instant> moved_to;

    /// Новая длительность, если она поменялась вместе с местом.
    std::optional<Lesson::Duration> moved_duration;

    friend bool operator==(const RecurrenceException&, const RecurrenceException&) = default;
};

/// Откуда взялось вхождение и что с ним не так.
enum class Placement : std::uint8_t {
    /// Обычный случай: правило поставило, часы не мешали.
    kExact,

    /// Перенесено рукой. Стоит там, куда перенесли, а не там, где считает правило.
    kMovedByHand,

    /// Местного времени в этот день не было: часы перевели вперёд. Момент —
    /// конец пропавшего промежутка, и репетитору это надо показать, а не
    /// подставить молча.
    kMissingAfterClockChange,

    /// Местное время в этот день было дважды: часы перевели назад. Взято
    /// ПЕРВОЕ, и это тоже видно.
    kTwiceOnTheClock,

    /// ГРАНИЦА СПИСКА, а не размещение.
    kBoundary,
};

std::string_view Name(Placement placement) noexcept;

/// Одно занятие серии — рассчитанное, а не хранимое.
struct Occurrence final {
    /// Местная дата, на которую вхождение ставит правило. У перенесённого —
    /// та же: по ней его и опознают.
    core::Date on;

    core::Instant starts_at;
    Lesson::Duration duration{};
    Placement placement{Placement::kExact};

    core::Instant EndsAt() const noexcept {
        return starts_at + duration;
    }

    friend bool operator==(const Occurrence&, const Occurrence&) = default;
};

/// СЕРИЯ ХРАНИТСЯ ПРАВИЛОМ, А НЕ РАЗВЁРНУТЫМ СПИСКОМ.
///
/// Сорок строк, созданных при заведении серии, ломаются на первом же переносе:
/// правило и список расходятся, и починить их можно только вручную. Здесь
/// хранится правило, а занятия считаются по запросу на нужный отрезок.
///
/// ВРЕМЯ ЗАДАНО В МЕСТНОЙ ЗОНЕ, А НЕ В UTC. «Каждый вторник в 18:00» — это
/// утверждение про часы репетитора: после перевода часов занятие остаётся в
/// 18:00 у него, а не уезжает на час. Серия, хранящая UTC, сдвигается дважды в
/// год и объясняет это ученику сама.
class RecurrenceSeries final {
public:
    static core::Result<RecurrenceSeries> Compose(core::SeriesId id,
                                                  core::TenantId tenant,
                                                  core::PersonId tutor,
                                                  std::vector<core::PersonId> participants,
                                                  RecurrenceRule rule,
                                                  core::Date starts_on,
                                                  core::LocalTime at,
                                                  core::TimeZone zone,
                                                  Lesson::Duration duration);

    const core::SeriesId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Tutor() const noexcept {
        return tutor_;
    }
    const std::vector<core::PersonId>& Participants() const noexcept {
        return participants_;
    }
    const RecurrenceRule& Rule() const noexcept {
        return rule_;
    }
    const core::Date& StartsOn() const noexcept {
        return starts_on_;
    }
    core::LocalTime At() const noexcept {
        return at_;
    }
    const core::TimeZone& Zone() const noexcept {
        return zone_;
    }
    Lesson::Duration LessonDuration() const noexcept {
        return duration_;
    }
    const std::vector<RecurrenceException>& Exceptions() const noexcept {
        return exceptions_;
    }

    /// Серия с ещё одним исключением. Второе исключение на ту же дату — отказ:
    /// «отменено и перенесено одновременно» не значит ничего.
    core::Result<RecurrenceSeries> With(RecurrenceException exception) const;

    /// ИЗМЕНИТЬ ПРАВИЛО — ЗНАЧИТ РАЗРЕЗАТЬ СЕРИЮ, А НЕ ПЕРЕПИСАТЬ ЕЁ.
    ///
    /// Прошедшие занятия не трогаются НЕ ПОТОМУ, ЧТО развёртка про них помнит,
    /// а потому, что старой серии больше нечего про них сказать: она кончается
    /// днём разреза, и после него не даёт вхождений вовсе. История защищена
    /// устройством, а не дисциплиной вызывающего.
    ///
    /// Возвращаются обе: прежняя, укороченная до дня перед разрезом, и новая —
    /// с новым правилом и новым идентификатором.
    core::Result<std::pair<RecurrenceSeries, RecurrenceSeries>> SplitAt(core::Date from,
                                                                        core::SeriesId next,
                                                                        RecurrenceRule rule) const;

private:
    RecurrenceSeries(core::SeriesId id,
                     core::TenantId tenant,
                     core::PersonId tutor,
                     std::vector<core::PersonId> participants,
                     RecurrenceRule rule,
                     core::Date starts_on,
                     core::LocalTime at,
                     core::TimeZone zone,
                     Lesson::Duration duration,
                     std::vector<RecurrenceException> exceptions);

    core::SeriesId id_;
    core::TenantId tenant_;
    core::PersonId tutor_;
    std::vector<core::PersonId> participants_;
    RecurrenceRule rule_;
    core::Date starts_on_;
    core::LocalTime at_;
    core::TimeZone zone_;
    Lesson::Duration duration_;
    std::vector<RecurrenceException> exceptions_;
};

/// ГОРИЗОНТ РАЗВЁРТКИ ПО УМОЛЧАНИЮ — ГОД.
///
/// Число живёт в динамическом конфиге (`PDR_SCHEDULE_EXPANSION_HORIZON`), а
/// сюда приходит параметром: ядро конфига не читает и читать не может. Здесь —
/// только умолчание для тех, кто зовёт развёртку из проверки.
inline constexpr core::Instant::Duration kDefaultHorizon =
    std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * 366});

/// РАЗВЁРТКА СЕРИИ НА ОТРЕЗОК.
///
/// Правила зоны приходят значением: базы IANA у ядра нет (`core::ZoneOffsets`),
/// достаёт её адаптер за портом. К КАЖДОМУ вхождению применяется
/// `core::Resolve` — именно поэтому серия и переживает перевод часов, оставаясь
/// на своём месте по местным часам.
///
/// ОТРЕЗОК ШИРЕ ГОРИЗОНТА — ОТКАЗ, а не молчаливое усечение. Запрос «покажи
/// расписание на десять лет» стоит сервису памяти и времени, а усечённый ответ
/// без предупреждения выглядит как «дальше занятий нет».
/// Сколько вхождений подряд развёртка берётся считать, прежде чем сказать, что
/// серия слишком длинная. Не то же, что горизонт: горизонт ограничивает
/// ЗАПРОШЕННЫЙ отрезок, а это — саму серию, у которой `UNTIL` стоит через
/// тридцать лет.
inline constexpr int kMaxOccurrences = 2000;

core::Result<std::vector<Occurrence>> Expand(const RecurrenceSeries& series,
                                             const core::TimeRange& window,
                                             const core::ZoneOffsets& offsets,
                                             core::Instant::Duration horizon = kDefaultHorizon);

}  // namespace pdr::scheduling
