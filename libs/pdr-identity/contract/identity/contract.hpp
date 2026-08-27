#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/types/ids.hpp"

namespace pdr::identity {

/// Что человек хочет сделать. РЕЕСТР ДЕЙСТВИЙ — ОДИН НА ВСЮ СИСТЕМУ.
///
/// Один затем, что «у каждого действия есть политика» — утверждение, которое
/// либо проверяется машиной, либо не существует. Проверить его можно только
/// пройдя по списку целиком, а список, разложенный по четырём областям, никто
/// целиком не обойдёт.
///
/// Область видна по приставке имени, а не по отдельному перечислению: второе
/// перечисление «область» пришлось бы держать в согласии с этим, и они разошлись
/// бы на первой же задаче.
enum class Action : std::uint8_t {
    kBookLesson,
    kCancelLesson,
    kRescheduleLesson,
    kViewSchedule,

    kViewInvoice,
    kPayInvoice,
    kIssueRefund,
    kSetTariff,

    kViewMaterial,
    kEditMaterial,
    kPublishMaterial,
    kAssignPlan,

    kViewProgress,
    kRecordAttempt,
    kExportProgress,
    kViewTenantProgress,

    kViewLessonRecording,
    kViewLessonTranscript,
    kViewAccessJournal,
    kManageGuardianAccess,

    kWriteReview,
    kManageAutoPayment,

    /// ГРАНИЦА СПИСКА, а не действие.
    ///
    /// Она здесь ради одной проверки: `kEveryAction` обязан содержать ровно
    /// столько же значений, сколько их в перечислении, и это сверяет
    /// static_assert. Завели действие и забыли про список — не собирается,
    /// а не запрещается молча в рантайме.
    kBoundary,
};

/// Машинный код действия: то же слово, что в матрице прав и в журнале.
std::string_view Name(Action action) noexcept;

std::optional<Action> ParseAction(std::string_view text);

/// Все действия подряд. Единственный способ обойти реестр целиком.
inline constexpr std::array<Action, 22> kEveryAction{
    Action::kBookLesson,          Action::kCancelLesson,
    Action::kRescheduleLesson,    Action::kViewSchedule,
    Action::kViewInvoice,         Action::kPayInvoice,
    Action::kIssueRefund,         Action::kSetTariff,
    Action::kViewMaterial,        Action::kEditMaterial,
    Action::kPublishMaterial,     Action::kAssignPlan,
    Action::kViewProgress,        Action::kRecordAttempt,
    Action::kExportProgress,      Action::kViewTenantProgress,
    Action::kViewLessonRecording, Action::kViewLessonTranscript,
    Action::kViewAccessJournal,   Action::kManageGuardianAccess,
    Action::kWriteReview,         Action::kManageAutoPayment,
};

static_assert(kEveryAction.size() == static_cast<std::size_t>(Action::kBoundary),
              "действие заведено, а в kEveryAction его нет: обход реестра пропустит его, "
              "и проверка «у каждого действия есть политика» станет ложью");

/// Почему отказано.
///
/// РЕШЕНИЕ НЕСЁТ ПРИЧИНУ, А НЕ `bool`. Разница не косметическая: человеку
/// нужно понять, что делать дальше, — «это не ваш кабинет» и «этот ученик не
/// ваш» приводят к разным следующим шагам. А разработчику при разборе жалобы
/// нужно отличать отказ по правилу от отказа из-за незаведённой политики,
/// и по «нельзя» это не отличается никак.
///
/// Список закрыт и короткий: подробность несёт не новый род отказа, а то, что
/// покажут человеку (`docs/product/voice.md`).
enum class DenyReason : std::uint8_t {
    /// Отказа нет. Разрешение — тоже решение, и оно тоже называет свою причину.
    kAllowed,

    /// Ресурс из чужого кабинета. Проверяется ДО любой политики: это не вопрос
    /// роли, а граница арендатора.
    kForeignTenant,

    /// Роли, которой такое действие даётся, у человека нет вовсе.
    kRoleMissing,

    /// Роль подходящая, но ресурс чужой: не он его ведёт, не о нём данные и не
    /// о том, кого он опекает.
    kNotYours,

    /// Опекун своего подопечного, но ЭТОТ УРОВЕНЬ ему не открывали. Записи
    /// занятий не открываются вместе с опекой — только отдельным согласием.
    kScopeMissing,

    /// Ученик стал взрослым, и слово теперь за ним. Отдельная причина, потому
    /// что идти надо не к репетитору, а к самому ученику: это единственный
    /// человек, который может открыть доступ обратно.
    kStudentGrewUp,

    /// ЕЩЁ НЕ ДОРОС. Не «нельзя» и не «не ваше»: право придёт САМО, в день
    /// рождения, и выдавать его никто не будет. Отдельная причина затем, что
    /// человеку надо сказать не «обратитесь», а «пока это делает за вас
    /// опекун», — единственный отказ в списке, который проходит сам собой.
    kTooYoung,

    /// ДЕЙСТВИЕ БЕЗ ПОЛИТИКИ. Это не отказ человеку, а ошибка настройки: кто-то
    /// завёл действие и не связал его с политикой. Значение по умолчанию —
    /// запрет, и о нём сообщают как о поломке.
    kNoPolicy,
};

std::string_view Name(DenyReason reason) noexcept;

/// Решение: можно или нет и почему.
struct PolicyDecision final {
    bool allowed{false};
    DenyReason reason{DenyReason::kNoPolicy};

    friend bool operator==(const PolicyDecision&, const PolicyDecision&) = default;
};

/// Разрешение. Причина названа и у него: тогда «решение без причины» не
/// выражается вовсе.
inline constexpr PolicyDecision Allowed() noexcept {
    return PolicyDecision{true, DenyReason::kAllowed};
}

inline constexpr PolicyDecision Denied(DenyReason reason) noexcept {
    return PolicyDecision{false, reason};
}

/// Над чем действие.
///
/// Ресурс описан ОТНОШЕНИЯМИ, а не своим содержимым: политике незачем знать,
/// что такое занятие, счёт или материал, — ей нужно знать, чей он. Поэтому
/// здесь нет ни типа ресурса, ни его полей, и `scheduling` не приходится
/// тащить в `identity`.
///
///   owner    кто его ведёт: репетитор занятия, выставивший счёт, автор материала;
///   subject  о ком он: ученик занятия, плательщик счёта, кому назначен материал.
///
/// Обоих может не быть — у сводки по кабинету нет ни того, ни другого.
struct Resource final {
    core::TenantId tenant;
    std::optional<core::PersonId> owner;
    std::optional<core::PersonId> subject;
};

/// Публичный контракт контекста identity — ЕДИНСТВЕННЫЙ его заголовок, который
/// другим контекстам разрешено включать. Всё остальное в модуле для них не
/// существует: каталог src в чужую сборку не попадает, а попытку включить
/// внутренность ловит scripts/check_layers.py.
///
/// Контракт говорит на платформенных типах (`core::`) и простых значениях.
/// Доменные value-объекты контекста границу не пересекают: чужой контекст не
/// обязан знать правила их разбора, а мы не обязаны хранить их форму навсегда.
/// `Action`, `DenyReason` и `PolicyDecision` — исключение и не нарушение
/// правила: они не описывают наши данные, они и ЕСТЬ вопрос и ответ. Разбирать
/// в них нечего, а без них вопрос не задать.
///
/// Роли в контракте нет намеренно. Спрашивающий называет СЕБЯ, а какие у него
/// роли — выясняет identity: иначе чужой контекст сначала узнаёт роли, потом
/// решает сам, и проверка прав расползается по хендлерам — ровно то, ради чего
/// всё это заведено.
class Contract {
public:
    Contract(const Contract&) = delete;
    Contract& operator=(const Contract&) = delete;

    virtual ~Contract() = default;

    /// Вправе ли `actor` действовать от имени `student`.
    ///
    /// Истина в двух случаях: он и есть этот ученик (самостоятельный взрослый —
    /// языки, подготовка в вуз: опекуна у него нет вовсе) либо между ними есть
    /// действующая опека.
    virtual bool MayActFor(const core::TenantId& tenant,
                           const core::PersonId& actor,
                           const core::PersonId& student) const = 0;

    /// Можно ли этому человеку это действие над этим ресурсом.
    ///
    /// ХЕНДЛЕР СПРАШИВАЕТ И НЕ ЗНАЕТ СОДЕРЖИМОГО ОТВЕТА. Новая роль или новое
    /// правило — это новая политика внутри identity, а не правка двадцати
    /// хендлеров, каждый из которых понял правило чуть-чуть по-своему.
    virtual PolicyDecision Decide(const core::TenantId& tenant,
                                  const core::PersonId& actor,
                                  Action action,
                                  const Resource& resource) const = 0;

protected:
    Contract() = default;
};

}  // namespace pdr::identity
