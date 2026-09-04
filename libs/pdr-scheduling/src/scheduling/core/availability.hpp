#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"

namespace pdr::scheduling {

/// Когда репетитор готов работать: день недели и часы ПО ЕГО ЧАСАМ.
///
/// Зона хранится вместе с правилом, а не берётся откуда-то при показе: «по
/// вторникам с десяти» — это утверждение про местное время того, кто его
/// написал, и переезд репетитора в другую зону не должен молча сдвинуть его
/// расписание на пять часов.
class AvailabilityRule final {
public:
    static core::Result<AvailabilityRule> Compose(core::Weekday day,
                                                  core::LocalTime from,
                                                  core::LocalTime to,
                                                  core::TimeZone zone);

    core::Weekday Day() const noexcept {
        return day_;
    }
    core::LocalTime From() const noexcept {
        return from_;
    }
    core::LocalTime To() const noexcept {
        return to_;
    }
    const core::TimeZone& Zone() const noexcept {
        return zone_;
    }

    friend bool operator==(const AvailabilityRule&, const AvailabilityRule&) = default;

private:
    AvailabilityRule(core::Weekday day,
                     core::LocalTime from,
                     core::LocalTime to,
                     core::TimeZone zone) noexcept
        : day_{day}, from_{from}, to_{to}, zone_{std::move(zone)} {}

    core::Weekday day_;
    core::LocalTime from_;
    core::LocalTime to_;
    core::TimeZone zone_;
};

/// Отдельный день, который живёт не по правилу: выходной или иные часы.
///
/// Пустой отрезок означает выходной. Это не «забыли заполнить»: «в этот день не
/// работаю» и «в этот день работаю с двух до четырёх» — два разных ответа, и
/// первый обязан выражаться, иначе выходной пришлось бы изображать отрезком
/// нулевой длины, которого не бывает.
struct AvailabilityException final {
    core::Date date;
    std::optional<core::TimeRange> instead;

    friend bool operator==(const AvailabilityException&, const AvailabilityException&) = default;
};

/// ЧТО ДОСТУПНОСТЬ ГОВОРИТ ПРО ЗАНЯТИЕ.
///
/// ЗАНЯТИЕ ВНЕ ДОСТУПНОСТИ НЕ ЗАПРЕЩЕНО. Репетитор вправе провести занятие в
/// воскресенье, даже если по воскресеньям он не работает, — продукт не мешает
/// человеку работать по-своему. Но такое занятие ОТМЕЧАЕТСЯ, и записать его
/// молча нельзя: сценарий требует явного подтверждения.
enum class AvailabilityVerdict : std::uint8_t {
    /// Занятие целиком внутри доступного времени.
    kInside,

    /// Занятие выходит за доступное время. Не отказ — повод спросить.
    kOutsideNeedsConfirmation,
};

std::string_view Name(AvailabilityVerdict verdict) noexcept;

/// Доступность репетитора: правила по дням недели плюс исключения по датам.
///
/// Исключение сильнее правила: на дату, у которой есть исключение, правила дня
/// недели не смотрят вовсе. Иначе «в эту субботу не работаю» пришлось бы
/// выражать удалением субботнего правила и возвращением его обратно.
class Availability final {
public:
    static core::Result<Availability> Compose(std::vector<AvailabilityRule> rules,
                                              std::vector<AvailabilityException> exceptions);

    const std::vector<AvailabilityRule>& Rules() const noexcept {
        return rules_;
    }
    const std::vector<AvailabilityException>& Exceptions() const noexcept {
        return exceptions_;
    }

    /// Внутри ли занятие. Правила зоны приходят значением: базы IANA у ядра нет
    /// (`core::ZoneOffsets`), а достаёт её адаптер за портом.
    ///
    /// Смотрится ВЕСЬ отрезок занятия, а не только его начало: занятие,
    /// начавшееся в последнюю доступную минуту и идущее час, выходит за
    /// доступность, и репетитор об этом узнать обязан.
    AvailabilityVerdict Covers(const core::TimeRange& lesson,
                               const core::ZoneOffsets& offsets) const;

private:
    Availability(std::vector<AvailabilityRule> rules,
                 std::vector<AvailabilityException> exceptions) noexcept
        : rules_{std::move(rules)}, exceptions_{std::move(exceptions)} {}

    std::vector<AvailabilityRule> rules_;
    std::vector<AvailabilityException> exceptions_;
};

}  // namespace pdr::scheduling
