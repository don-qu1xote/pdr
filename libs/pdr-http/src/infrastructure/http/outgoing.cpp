#include "infrastructure/http/outgoing.hpp"

#include <utility>

#include <userver/clients/http/error.hpp>
#include <userver/clients/http/response.hpp>
#include <userver/logging/log.hpp>

namespace pdr::infrastructure::http {

Outgoing::Outgoing(std::string direction,
                   userver::clients::http::Client& client,
                   std::chrono::milliseconds timeout,
                   int attempts,
                   userver::utils::RetryBudget& budget,
                   userver::utils::TokenBucket& quota) noexcept
    : direction_{std::move(direction)},
      client_{client},
      timeout_{timeout},
      attempts_{attempts},
      budget_{budget},
      quota_{quota} {}

std::optional<Outgoing::Answer> Outgoing::Send(Repeatable repeatable, Build build) const {
    if (!quota_.Obtain()) {
        LOG_WARNING() << "квота направления исчерпана, наружу не идём: " << direction_;
        return std::nullopt;
    }

    const bool may_repeat = repeatable == Repeatable::kYes && attempts_ > 1 && budget_.CanRetry();

    auto request = build(client_);
    request.timeout(timeout_);
    request.SetDestinationMetricName(direction_);
    request.retry(static_cast<short>(may_repeat ? attempts_ : 1));

    try {
        auto answer = request.perform();
        if (answer->status_code() >= userver::clients::http::Status::kInternalServerError) {
            budget_.AccountFail();
            return std::nullopt;
        }
        budget_.AccountOk();
        return answer;
    } catch (const userver::clients::http::BaseException& failure) {
        budget_.AccountFail();
        LOG_WARNING() << "направление не ответило: " << direction_ << ": " << failure.what();
        return std::nullopt;
    }
}

}  // namespace pdr::infrastructure::http
