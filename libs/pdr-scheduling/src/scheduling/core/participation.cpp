#include "scheduling/core/participation.hpp"

namespace pdr::scheduling {

std::string_view Name(PaymentState state) noexcept {
    switch (state) {
        case PaymentState::kUnpaid:
            return "unpaid";
        case PaymentState::kPaid:
            return "paid";
        case PaymentState::kBoundary:
            break;
    }
    return "unpaid";
}

std::string_view Name(Attendance attendance) noexcept {
    switch (attendance) {
        case Attendance::kExpected:
            return "expected";
        case Attendance::kAttended:
            return "attended";
        case Attendance::kMissed:
            return "missed";
        case Attendance::kBoundary:
            break;
    }
    return "expected";
}

std::string_view Name(ParticipationState state) noexcept {
    switch (state) {
        case ParticipationState::kJoined:
            return "joined";
        case ParticipationState::kWithdrawn:
            return "withdrawn";
        case ParticipationState::kBoundary:
            break;
    }
    return "joined";
}

Participation Participation::Joined(core::PersonId person) {
    return Participation{std::move(person),
                         std::nullopt,
                         PaymentState::kUnpaid,
                         Attendance::kExpected,
                         ParticipationState::kJoined};
}

Participation Participation::Priced(core::Money price) const {
    return Participation{person_, std::move(price), payment_, attendance_, state_};
}

Participation Participation::Paid() const {
    return Participation{person_, price_, PaymentState::kPaid, attendance_, state_};
}

Participation Participation::Came() const {
    return Participation{person_, price_, payment_, Attendance::kAttended, state_};
}

Participation Participation::Missed() const {
    return Participation{person_, price_, payment_, Attendance::kMissed, state_};
}

Participation Participation::Withdrawn() const {
    return Participation{person_, price_, payment_, attendance_, ParticipationState::kWithdrawn};
}

}  // namespace pdr::scheduling
