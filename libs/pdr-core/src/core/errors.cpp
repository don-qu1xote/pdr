#include "core/errors.hpp"

namespace pdr::core {

std::string_view Name(ErrorKind kind) noexcept {
    switch (kind) {
        case ErrorKind::kValidation:
            return "validation";
        case ErrorKind::kNotFound:
            return "not_found";
        case ErrorKind::kConflict:
            return "conflict";
        case ErrorKind::kForbidden:
            return "forbidden";
        case ErrorKind::kBoundary:
            break;
    }
    return "validation";
}

}  // namespace pdr::core
