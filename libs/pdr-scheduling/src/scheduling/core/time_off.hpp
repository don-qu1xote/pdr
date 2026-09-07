#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "events/scheduling/time_off.hpp"

namespace pdr::scheduling {

/// Причина и решение берутся из реестра событий: оба уезжают подписчику, и
/// второго перечисления с теми же значениями заводить нельзя — они разойдутся в
/// первый же день. Ровно так же здесь живёт `RetentionReason`.
using TimeOffReason = events::scheduling::TimeOffReason;
using TimeOffDecision = events::scheduling::TimeOffDecision;

/// ЧТО ДЕЛАТЬ С СЕРИЯМИ — ОТДЕЛЬНЫЙ ВОПРОС, А НЕ ЧАСТЬ РЕШЕНИЯ ПО ЗАНЯТИЯМ.
///
/// Отдельный потому, что ответы на него другие. Занятие — вещь одиночная: его
/// можно отменить, подвинуть или оставить. Серия — обещание на месяцы вперёд, и
/// «отменить серию» на время отпуска не значит ничего: её либо пропускают на эти
/// две недели, либо целиком отодвигают.
enum class SeriesDecision : std::uint8_t {
    /// ПРОПУСТИТЬ вхождения, попавшие в перерыв. УМОЛЧАНИЕ.
    ///
    /// Умолчание именно это, а не сдвиг, и выбрано оно не монеткой. Пропуск
    /// меняет две даты и не трогает остальные тридцать; сдвиг переписывает
    /// расписание до конца года у всех участников сразу. Умолчанием ставят то,
    /// чего не жалко, если человек нажал не глядя.
    kSkip,

    /// СДВИНУТЬ остаток серии за перерыв целиком, ничего не потеряв.
    kShift,

    /// ГРАНИЦА СПИСКА, а не решение.
    kBoundary,
};

std::string_view Name(SeriesDecision decision) noexcept;

std::optional<SeriesDecision> ParseSeriesDecision(std::string_view text);
std::optional<TimeOffReason> ParseTimeOffReason(std::string_view text);
std::optional<TimeOffDecision> ParseTimeOffDecision(std::string_view text);

/// ПЕРЕРЫВ: ОТПУСК, БОЛЕЗНЬ, КАНИКУЛЫ — ОДНО И ТО ЖЕ УСТРОЙСТВО.
///
/// «Репетитор в отпуске» и «ученик на каникулах» — это один механизм с двух
/// сторон, а не две функции. Отсюда и поле: перерыв у ЧЕЛОВЕКА, а не у
/// репетитора. Какой стороной он стоит в занятии, видно из самого занятия, и
/// сценарию не приходится спрашивать роли ни у кого.
///
/// ЗАДНИМ ЧИСЛОМ — ПО УСТРОЙСТВУ, А НЕ ПО СНИСХОЖДЕНИЮ.
///
/// В доводах `Compose` нет `now`, и это главное свойство типа. Запретить период
/// в прошлом здесь просто нечем: сравнить не с чем. А запрещать его нельзя,
/// потому что заводят перерыв ровно тогда, когда он уже идёт: человек заболел в
/// понедельник и дошёл до телефона в среду. Продукт, требующий предупредить
/// заранее, в этот момент бесполезен.
///
/// ГРАНИЦЫ — МЕСТНЫЕ ДАТЫ, ВКЛЮЧИТЕЛЬНО ОБЕ. «С первого по четырнадцатое» — это
/// то, что человек говорит, и хранить это надо тем же. Момента здесь нет
/// намеренно: дата станет моментом только по правилам зоны, а таблицы переводов
/// у ядра нет (`core::ZoneOffsets` приходит значением). Зона лежит рядом —
/// та, в которой человек НАЗВАЛ даты.
class TimeOff final {
public:
    /// ПОТОЛОК ДЛИНЫ — год с небольшим, и он не мера строгости.
    ///
    /// Перерыв длиннее года не означает ничего: расписания на такую даль ещё
    /// нет, а решение по занятиям применять не к чему. Зато потолок ловит
    /// описку в дате, за которой стоит отмена всего расписания разом, — и
    /// ловит её ДО того, как она случилась.
    ///
    /// Описку ровно в год (2027 вместо 2026) он не поймает, и не должен:
    /// годовой перерыв бывает настоящим — декрет, переезд, армия. Число здесь
    /// отделяет осмысленное от бессмысленного, а не правдоподобное от
    /// подозрительного.
    static constexpr int kMaxDays = 400;

    static core::Result<TimeOff> Compose(core::TimeOffId id,
                                         core::TenantId tenant,
                                         core::PersonId person,
                                         core::Date from,
                                         core::Date to,
                                         std::optional<TimeOffReason> reason,
                                         core::TimeZone zone,
                                         core::PersonId declared_by);

    const core::TimeOffId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Person() const noexcept {
        return person_;
    }
    const core::Date& From() const noexcept {
        return from_;
    }

    /// ПОСЛЕДНИЙ ДЕНЬ ПЕРЕРЫВА, ВКЛЮЧИТЕЛЬНО. Не «день выхода»: их путают, и
    /// путаница стоит одного занятия.
    const core::Date& To() const noexcept {
        return to_;
    }
    const std::optional<TimeOffReason>& Reason() const noexcept {
        return reason_;
    }
    const core::TimeZone& Zone() const noexcept {
        return zone_;
    }
    const core::PersonId& DeclaredBy() const noexcept {
        return declared_by_;
    }

    /// Первый рабочий день после перерыва.
    core::Date ResumesOn() const;

    /// Сколько дней длится перерыв, считая оба конца.
    int Days() const noexcept;

    /// Внутри ли эта местная дата. Сравнение дат с датами — зона тут не нужна
    /// вовсе: и перерыв, и вхождение серии живут по часам одного человека.
    bool Covers(const core::Date& date) const noexcept;

    friend bool operator==(const TimeOff&, const TimeOff&) = default;

private:
    TimeOff(core::TimeOffId id,
            core::TenantId tenant,
            core::PersonId person,
            core::Date from,
            core::Date to,
            std::optional<TimeOffReason> reason,
            core::TimeZone zone,
            core::PersonId declared_by) noexcept
        : id_{std::move(id)},
          tenant_{std::move(tenant)},
          person_{std::move(person)},
          from_{from},
          to_{to},
          reason_{reason},
          zone_{std::move(zone)},
          declared_by_{std::move(declared_by)} {}

    core::TimeOffId id_;
    core::TenantId tenant_;
    core::PersonId person_;
    core::Date from_;
    core::Date to_;
    std::optional<TimeOffReason> reason_;
    core::TimeZone zone_;
    core::PersonId declared_by_;
};

/// НА СКОЛЬКО ЦЕЛЫХ НЕДЕЛЬ ОТОДВИГАЕТ ЭТОТ ПЕРЕРЫВ.
///
/// Недель, а не дней, и это не округление ради простоты. Занятия репетиторства
/// стоят по дням недели: «каждый вторник в 18:00» — и у серии (`FREQ=WEEKLY` —
/// единственная поддержанная частота), и в голове у ученика. Сдвиг на
/// одиннадцать дней превращает вторник в субботу; это не то же расписание,
/// отложенное, а другое.
///
/// Берётся наименьшее число недель, которое кладёт сдвинутое занятие СТРОГО
/// после перерыва: перерыв в три дня — одна неделя, в восемь — две.
int WeeksOver(const TimeOff& period) noexcept;

}  // namespace pdr::scheduling
