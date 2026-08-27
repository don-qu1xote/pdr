#include "identity/application/notify_guardian_of_act.hpp"

#include "events/identity/ward_acted_alone.hpp"

namespace pdr::identity {
namespace {

/// Поступок домена — кодом события. Разбор полный: заведёшь поступок и забудешь
/// сообщить о нём — не соберётся, а не промолчит в проде.
pdr::events::identity::WardAct Published(IndependentAct act) noexcept {
    switch (act) {
        case IndependentAct::kLessonRescheduled:
            return pdr::events::identity::WardAct::kLessonRescheduled;
        case IndependentAct::kLessonCancelled:
            return pdr::events::identity::WardAct::kLessonCancelled;
        case IndependentAct::kReviewWritten:
        case IndependentAct::kBoundary:
            return pdr::events::identity::WardAct::kReviewWritten;
    }
    return pdr::events::identity::WardAct::kReviewWritten;
}

}  // namespace

NotifyGuardianOfAct::NotifyGuardianOfAct(const ports::GuardianshipRepository& guardianships,
                                         const application::ports::Clock& clock,
                                         events::Bus& bus) noexcept
    : guardianships_{guardianships}, clock_{clock}, bus_{bus} {}

core::Result<int> NotifyGuardianOfAct::Execute(const NotifyGuardianOfActRequest& request) const {
    const auto now = clock_.Now();
    int told = 0;

    for (const auto& guardian : guardianships_.GuardiansOf(request.tenant, request.student)) {
        bus_.Publish(pdr::events::identity::WardActedAlone{
            pdr::events::Envelope{request.tenant, now},
            guardian,
            request.student,
            Published(request.act),
        });
        ++told;
    }

    return told;
}

}  // namespace pdr::identity
