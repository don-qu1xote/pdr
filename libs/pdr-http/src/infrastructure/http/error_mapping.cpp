#include "infrastructure/http/error_mapping.hpp"

#include <string>

#include "infrastructure/http/idempotency.hpp"

namespace pdr::infrastructure::http {
namespace {

constexpr int kBadRequest = 400;
constexpr int kUnauthorized = 401;
constexpr int kForbidden = 403;
constexpr int kNotFound = 404;
constexpr int kConflict = 409;
constexpr int kUnprocessable = 422;
constexpr int kInternal = 500;
constexpr int kOk = 200;

constexpr std::string_view kUnidentifiedCode = "not_identified";
constexpr std::string_view kMalformedCode = "request_malformed";
constexpr std::string_view kKeyRequiredCode = "idempotency_key_required";
constexpr std::string_view kKeyInFlightCode = "idempotency_key_in_flight";

Problem Build(std::string_view code,
              std::string_view title,
              int status,
              std::string_view detail,
              const Occasion& occasion) {
    return Problem{ProblemType(code),
                   std::string{title},
                   status,
                   std::string{detail},
                   std::string{occasion.instance},
                   std::string{occasion.request_id},
                   std::nullopt};
}

}  // namespace

int StatusOf(core::ErrorKind kind) noexcept {
    switch (kind) {
        case core::ErrorKind::kValidation:
            return kUnprocessable;
        case core::ErrorKind::kNotFound:
            return kNotFound;
        case core::ErrorKind::kConflict:
            return kConflict;
        case core::ErrorKind::kForbidden:
            return kForbidden;
        case core::ErrorKind::kBoundary:
            break;
    }
    return kInternal;
}

std::string_view TitleOf(core::ErrorKind kind) noexcept {
    switch (kind) {
        case core::ErrorKind::kValidation:
            return "Данные не проходят правило";
        case core::ErrorKind::kNotFound:
            return "Не найдено";
        case core::ErrorKind::kConflict:
            return "Состояние не позволяет";
        case core::ErrorKind::kForbidden:
            return "Нельзя";
        case core::ErrorKind::kBoundary:
            break;
    }
    return "Поломка";
}

Problem AsProblem(const core::Error& error, const Occasion& occasion) {
    return Build(
        error.Code(), TitleOf(error.Kind()), StatusOf(error.Kind()), error.Detail(), occasion);
}

int StatusOf(identity::DenyReason reason) noexcept {
    switch (reason) {
        case identity::DenyReason::kAllowed:
            return kOk;
        case identity::DenyReason::kForeignTenant:
            return kNotFound;
        case identity::DenyReason::kRoleMissing:
        case identity::DenyReason::kNotYours:
        case identity::DenyReason::kScopeMissing:
        case identity::DenyReason::kStudentGrewUp:
        case identity::DenyReason::kTooYoung:
            return kForbidden;
        case identity::DenyReason::kNoPolicy:
        case identity::DenyReason::kBoundary:
            break;
    }
    return kInternal;
}

std::string_view TitleOf(identity::DenyReason reason) noexcept {
    switch (reason) {
        case identity::DenyReason::kAllowed:
            return "Можно";
        case identity::DenyReason::kForeignTenant:
            return "Не найдено";
        case identity::DenyReason::kRoleMissing:
        case identity::DenyReason::kNotYours:
        case identity::DenyReason::kScopeMissing:
        case identity::DenyReason::kStudentGrewUp:
        case identity::DenyReason::kTooYoung:
            return "Нельзя";
        case identity::DenyReason::kNoPolicy:
        case identity::DenyReason::kBoundary:
            break;
    }
    return "Поломка";
}

Problem AsProblem(const identity::PolicyDecision& decision, const Occasion& occasion) {
    return Build(identity::Name(decision.reason),
                 TitleOf(decision.reason),
                 StatusOf(decision.reason),
                 identity::Name(decision.reason),
                 occasion);
}

Problem Unidentified(const core::Error& error, const Occasion& occasion) {
    Problem problem = Build(kUnidentifiedCode, "Кто вы", kUnauthorized, error.Detail(), occasion);
    problem.type = ProblemType(error.Code());
    return problem;
}

Problem Malformed(const core::Error& error, std::string_view field, const Occasion& occasion) {
    Problem problem =
        Build(kMalformedCode, "Запрос не разобрался", kBadRequest, error.Detail(), occasion);
    problem.type = ProblemType(error.Code());
    if (!field.empty()) {
        problem.field = std::string{field};
    }
    return problem;
}

Problem KeyRequired(std::string_view header, const Occasion& occasion) {
    return Build(kKeyRequiredCode,
                 "Нужен ключ повтора",
                 kBadRequest,
                 "заголовок " + std::string{header} +
                     " обязателен: без него повтор по "
                     "оборванной связи выполнит операцию второй раз",
                 occasion);
}

Problem KeyInFlight(std::string_view key, const Occasion& occasion) {
    return Build(kKeyInFlightCode,
                 "Тот же запрос уже выполняется",
                 kConflict,
                 "ключ «" + std::string{key} +
                     "» занят запросом, который ещё идёт. "
                     "Повторите через несколько секунд: параллельно операция не выполняется",
                 occasion);
}

}  // namespace pdr::infrastructure::http
