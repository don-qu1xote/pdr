#include "identity/application/policies/subject.hpp"

namespace pdr::identity {

std::string_view Name(Tie tie) noexcept {
    switch (tie) {
        case Tie::kMine:
            return "mine";
        case Tie::kAboutMe:
            return "about_me";
        case Tie::kInMyCare:
            return "in_my_care";
        case Tie::kNone:
            return "none";
    }
    return "none";
}

Tie TieBetween(const core::PersonId& person,
               const Resource& resource,
               bool looks_after_subject) noexcept {
    if (resource.owner.has_value() && *resource.owner == person) {
        return Tie::kMine;
    }
    if (resource.subject.has_value() && *resource.subject == person) {
        return Tie::kAboutMe;
    }
    if (resource.subject.has_value() && looks_after_subject) {
        return Tie::kInMyCare;
    }
    return Tie::kNone;
}

}  // namespace pdr::identity
