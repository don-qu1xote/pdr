#include "identity/core/contact_list.hpp"

#include <algorithm>
#include <unordered_set>

namespace pdr::identity {
namespace {

constexpr std::string_view kSeparators = "\n\r,;\t";
constexpr std::string_view kSpaces = " \t\v\f";

std::string_view Trim(std::string_view text) {
    const auto from = text.find_first_not_of(kSpaces);
    if (from == std::string_view::npos) {
        return {};
    }
    const auto to = text.find_last_not_of(kSpaces);
    return text.substr(from, to - from + 1);
}

/// «Иван Петров <ivan@example.ru>» — то, что получается при копировании из
/// почтового клиента. Берём то, что в скобках; всё остальное человек написал
/// для себя.
std::string_view Addressed(std::string_view line) {
    const auto opening = line.find('<');
    const auto closing = line.find('>', opening == std::string_view::npos ? 0 : opening);
    if (opening == std::string_view::npos || closing == std::string_view::npos ||
        closing < opening) {
        return line;
    }
    return Trim(line.substr(opening + 1, closing - opening - 1));
}

}  // namespace

std::string_view Name(ContactVerdict verdict) noexcept {
    switch (verdict) {
        case ContactVerdict::kReady:
            return "ready";
        case ContactVerdict::kMalformed:
            return "malformed";
        case ContactVerdict::kRepeatedInList:
            return "repeated_in_list";
        case ContactVerdict::kAlreadyInvited:
            return "already_invited";
        case ContactVerdict::kAlreadyEnrolled:
            return "already_enrolled";
        case ContactVerdict::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<ContactVerdict> ParseContactVerdict(std::string_view text) {
    for (const auto verdict : kEveryContactVerdict) {
        if (Name(verdict) == text) {
            return verdict;
        }
    }
    return std::nullopt;
}

ContactList ContactList::Parse(std::string_view pasted) {
    std::vector<Contact> lines;
    std::unordered_set<std::string> seen;

    std::size_t from = 0;
    while (from <= pasted.size() && lines.size() < kMostLines) {
        const auto to = pasted.find_first_of(kSeparators, from);
        const auto piece = Trim(pasted.substr(from, to == std::string_view::npos ? to : to - from));

        if (!piece.empty()) {
            auto mail = Email::Parse(Addressed(piece));
            if (!mail) {
                lines.emplace_back(std::string{piece}, std::nullopt, ContactVerdict::kMalformed);
            } else if (!seen.insert(mail.Value().Value()).second) {
                lines.emplace_back(
                    std::string{piece}, mail.Value(), ContactVerdict::kRepeatedInList);
            } else {
                lines.emplace_back(std::string{piece}, mail.Value(), ContactVerdict::kReady);
            }
        }

        if (to == std::string_view::npos) {
            break;
        }
        from = to + 1;
    }

    return ContactList{std::move(lines)};
}

std::size_t ContactList::Ready() const noexcept {
    return static_cast<std::size_t>(std::count_if(lines_.begin(), lines_.end(), [](const auto& c) {
        return c.Verdict() == ContactVerdict::kReady;
    }));
}

}  // namespace pdr::identity
