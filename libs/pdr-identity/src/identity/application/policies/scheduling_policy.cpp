#include "identity/application/policies/scheduling_policy.hpp"

#include <array>

#include "identity/application/policies/capability_policy.hpp"
#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kMayBook{
    AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, StudentChoosingTutor(), GuardianInSchedule()};

const AnyOf kMayMove{
    AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, StudentMovingOwnSlots(), GuardianInSchedule()};

const AnyOf kOwnAffairs{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}},
                        AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}},
                        GuardianInSchedule()};

/// СВОЁ РАСПИСАНИЕ ЦЕЛИКОМ, ОБЕИМИ СТОРОНАМИ СРАЗУ.
///
/// Ветви выше спрашивают про ОДНУ сторону: «занятия, которые он ведёт» —
/// репетитору, «занятия, на которые он записан» — ученику. Расписание человека
/// шире: у ленты подписки (ADR-0024) стороны нет вовсе, в календарь уезжает всё
/// сразу, и ресурс называет одного и того же человека и ведущим, и участником.
///
/// У репетитора такой ресурс уже разрешён ветвью выше — он в нём ведущий.
/// Ученику же он достаётся отношением `kMine`, которого у него не бывает нигде
/// больше: своё расписание он смотрит потому, что оно своё.
///
/// РОЛЬ ЗДЕСЬ ВСЁ РАВНО СПРАШИВАЕТСЯ. Без неё правило разрешало бы «своё» и
/// тому, у кого роли нет ни одной, — а приглашённый, не пришедший по ссылке,
/// не смотрит ничего (`policies_test.cpp`, NobodyWithoutARoleGetsAnything).
///
/// Опекуна здесь нет намеренно: своего расписания у него не бывает, а лента
/// подопечного — не «своё» (docs/architecture/calendar-feed.md, «Чего здесь
/// нет»). Пустая лента вместо отказа объясняла бы ему меньше, чем отказ.
const AllOf kMayLookAtHisOwn{HasRole{Role::kStudent}, Tied{Tie::kMine}};

const AnyOf kMayLook{kOwnAffairs, kMayLookAtHisOwn, HasRole{Role::kOwner}};

/// ЧАСЫ РАБОТЫ ЗАДАЁТ ТОТ, ЧЬИ ОНИ. Ни владелец практики, ни опекун сюда не
/// входят: «когда я работаю» — утверждение репетитора о себе, и подставить его
/// за него не может никто. Смотреть эти часы при этом вправе многие — на то
/// отдельное действие.
const AllOf kMayOfferHours{HasRole{Role::kTutor}, Tied{Tie::kMine}};

constexpr std::array kActions{
    Action::kBookLesson,
    Action::kCancelLesson,
    Action::kRescheduleLesson,
    Action::kViewSchedule,
    Action::kSetAvailability,
};

}  // namespace

std::span<const Action> SchedulingPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision SchedulingPolicy::Decide(const Subject& subject,
                                        Action action,
                                        const Resource& resource) const {
    switch (action) {
        case Action::kBookLesson:
            return kMayBook.Decide(subject, action, resource);
        case Action::kCancelLesson:
        case Action::kRescheduleLesson:
            return kMayMove.Decide(subject, action, resource);
        case Action::kViewSchedule:
            return kMayLook.Decide(subject, action, resource);
        case Action::kSetAvailability:
            return kMayOfferHours.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
