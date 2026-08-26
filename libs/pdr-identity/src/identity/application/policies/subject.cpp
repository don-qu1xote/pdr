#include "identity/application/policies/subject.hpp"

namespace pdr::identity {

std::string_view Name(Tie tie) noexcept {
    switch (tie) {
        case Tie::kMine:
            return "mine";
        case Tie::kAboutMe:
            return "about_me";
        case Tie::kMyWard:
            return "my_ward";
        case Tie::kNone:
            return "none";
    }
    return "none";
}

Tie TieBetween(const core::PersonId& person,
               const Resource& resource,
               bool guards_subject) noexcept {
    if (resource.owner.has_value() && *resource.owner == person) {
        return Tie::kMine;
    }
    if (resource.subject.has_value() && *resource.subject == person) {
        return Tie::kAboutMe;
    }
    if (resource.subject.has_value() && guards_subject) {
        return Tie::kMyWard;
    }
    return Tie::kNone;
}

}  // namespace pdr::identity
