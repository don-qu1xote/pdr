#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::scheduling {

/// КТО ПРОСИТ. Владелец расписания своими окнами не ограничен.
///
/// Окна защищают репетитора от того, что с его временем сделают другие, — и
/// защищать его от самого себя они не должны. Договорился голосом и ставит
/// занятие через час — это его расписание и его решение; отказ здесь означал бы,
/// что продукт знает про его день больше, чем он.
enum class BookingSide : std::uint8_t {
    /// Репетитор со своим собственным расписанием.
    kScheduleOwner,

    /// Все остальные: ученик, опекун, кто угодно снаружи.
    kIncoming,
};

/// ЧТО ПРОСЯТ СДЕЛАТЬ. Три действия, три отдельных окна.
///
/// Одно окно на все три не годится: записаться за два часа до начала — обычное
/// дело, а перенести за два часа — уже перекладывание своей несобранности на
/// репетитора, который к этому занятию готовился. Разные поступки меряются
/// разными мерами.
enum class BookingAction : std::uint8_t {
    kBook,
    kReschedule,
    kCancel,

    /// ГРАНИЦА СПИСКА, а не действие.
    kBoundary,
};

std::string_view Name(BookingAction action) noexcept;

/// Все действия подряд. Единственный способ обойти список целиком.
inline constexpr std::array<BookingAction, 3> kEveryBookingAction{
    BookingAction::kBook,
    BookingAction::kReschedule,
    BookingAction::kCancel,
};

static_assert(kEveryBookingAction.size() == static_cast<std::size_t>(BookingAction::kBoundary),
              "действие заведено, а в kEveryBookingAction его нет: обход пропустит его, и "
              "«у каждого действия есть своё окно» станет непроверяемым");

/// ОКНА БРОНИРОВАНИЯ: насколько заранее можно занять чужое время и подвинуть его.
///
/// БЕЗ НИХ РАСПИСАНИЕ РЕПЕТИТОРА — ЧУЖАЯ СОБСТВЕННОСТЬ. Записаться за пять
/// минут до начала, перенести за минуту, занять весь следующий год — всё это
/// делает не ученик со злым умыслом, а обычный человек, которому никто не сказал
/// «так нельзя». Сказать должен продукт, и сказать до того, как время выбрано.
///
/// ОКНО — НЕ ПОЛИТИКА УДЕРЖАНИЯ. Окно отвечает на «можно ли это в принципе»,
/// политика (`CancellationPolicy`) — на «сколько это стоит». Их постоянно
/// смешивают, и смешение выходит боком в обе стороны: «отменить нельзя» вместо
/// «отменить можно, но платно» — это ученик, который просто не придёт, а
/// «отменить бесплатно» вместо «отменить нельзя» — репетитор, у которого час
/// пропал за минуту до начала. Здесь только первое.
///
/// КАЖДОЕ ОКНО ДОПУСКАЕТ «БЕЗ ОГРАНИЧЕНИЯ» (`std::nullopt`), и это не то же
/// самое, что ноль. Ноль — «можно вплоть до самого начала», решение; пустота —
/// «мы про это не договаривались», и договариваться человека никто не заставит.
/// Умолчания приходят из динамического конфига, а не отсюда: захардкоженное
/// окно — это спор с учеником, который нельзя решить, не выкатив сборку.
class BookingWindows final {
public:
    using Notice = std::chrono::minutes;

    /// Ничего не ограничено. Слабее этого набора не бывает — от него отсчитываются
    /// ослабления, и он же остаётся, когда настройки не завели вовсе.
    static BookingWindows Anything() noexcept;

    /// Собрать окна. Отказ — обычное значение: отрицательный срок приходит из
    /// настройки, которую заполнил человек.
    static core::Result<BookingWindows> Compose(std::optional<Notice> book,
                                                std::optional<Notice> horizon,
                                                std::optional<Notice> reschedule,
                                                std::optional<Notice> cancel);

    /// За сколько до начала занятие ещё можно записать.
    std::optional<Notice> MinNoticeBook() const noexcept {
        return book_;
    }

    /// Насколько далеко вперёд можно записываться. Считается ОТ «СЕЙЧАС»
    /// вперёд, в отличие от остальных трёх: те отсчитываются от начала занятия
    /// назад.
    std::optional<Notice> MaxHorizonBook() const noexcept {
        return horizon_;
    }

    std::optional<Notice> MinNoticeReschedule() const noexcept {
        return reschedule_;
    }
    std::optional<Notice> MinNoticeCancel() const noexcept {
        return cancel_;
    }

    friend bool operator==(const BookingWindows&, const BookingWindows&) = default;

private:
    BookingWindows(std::optional<Notice> book,
                   std::optional<Notice> horizon,
                   std::optional<Notice> reschedule,
                   std::optional<Notice> cancel) noexcept
        : book_{book}, horizon_{horizon}, reschedule_{reschedule}, cancel_{cancel} {}

    std::optional<Notice> book_;
    std::optional<Notice> horizon_;
    std::optional<Notice> reschedule_;
    std::optional<Notice> cancel_;
};

/// МОЖНО ЛИ ДЕЙСТВИЕ. Доменное правило, а не условие в хендлере.
///
/// Здесь оно по той же причине, по какой здесь `Overlaps`: правило, живущее в
/// обработчике запроса, соблюдается ровно на том пути, где его написали, — и
/// первый же второй путь (перенос из серии, запись из мобильного, будущая ручка
/// администратора) обойдёт его молча.
///
/// ГРАНИЦА ВХОДИТ В ОКНО. Запись ровно за два часа проходит: «за два часа»
/// значит «за два часа», и человеку, нажавшему кнопку секунда в секунду, не
/// объясняют, что имелось в виду «за два часа и одну секунду». То же правило,
/// что у окна бесплатной отмены (`CancellationPolicy::Free`), и то же по
/// причине.
core::Result<void> Allows(const BookingWindows& windows,
                          BookingAction action,
                          BookingSide side,
                          core::Instant starts_at,
                          core::Instant now);

/// ДЕЙСТВУЮЩИЕ ОКНА ПАРЫ: умолчания практики, ослабленные для этого ученика.
///
/// У постоянного ученика третий год обычно другие права, чем у пришедшего
/// вчера, — и репетитор вправе ему их дать. Дать, а не отнять: ослабление здесь
/// разрешено, ужесточение отклоняется. Персональная строгость — это правило,
/// которое ученик не может ни увидеть заранее, ни оспорить, и заводить такую
/// возможность значит заводить её и для тех, кто применит её без разбора.
///
/// Ослабление — ПОЛНЫЙ набор окон, а не поправка к нему. Поправка «это поле
/// оставить как есть» требует трёх состояний у каждого окна вместо двух, и
/// первое же «как есть» разъезжается с тем, что было в умолчаниях полгода назад.
core::Result<BookingWindows> Relax(const BookingWindows& base,
                                   const std::optional<BookingWindows>& relief);

/// ЧТО ПОКАЗАТЬ ИЗ СВОБОДНЫХ СЛОТОВ. Недоступное не показывается вовсе.
///
/// Слот, который видно и на который нельзя записаться, — это отказ, отложенный
/// до нажатия. Человек выбирает время, отвечает на вопрос «когда вам удобно», и
/// узнаёт, что так было нельзя, уже потратив выбор. Правильный ответ здесь —
/// пустое место, а не сообщение об ошибке.
///
/// Фильтр смотрит на ОКНО ЗАПИСИ: перенос и отмена относятся к уже
/// существующему занятию, а не к свободному слоту.
std::vector<core::Instant> Offered(std::span<const core::Instant> slots,
                                   const BookingWindows& windows,
                                   BookingSide side,
                                   core::Instant now);

/// Ослабление окон для одной пары: что дали, кому и кто дал.
///
/// Кто и когда — не украшение: «мне разрешили» через полгода превращается в
/// спор, и разрешает его строка, а не память участников.
struct BookingRelief final {
    core::TenantId tenant;
    core::PersonId tutor;
    core::PersonId student;
    BookingWindows windows;
    core::PersonId granted_by;
    core::Instant granted_at;
};

}  // namespace pdr::scheduling
