#include "infrastructure/http/postgres_idempotency_keys.hpp"

#include <optional>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::infrastructure::http {
namespace {

using infrastructure::db::AsTimestamptz;

}  // namespace

core::Result<pdr::http::Claim> PostgresIdempotencyKeys::Take(
    db::ScopedTenantContext& session,
    const core::TenantId& tenant,
    const pdr::http::IdempotencyKey& key,
    const pdr::http::RequestFingerprint& fingerprint,
    core::Instant expires_at) {
    const auto result = session.Session().Execute(sql::kHttpIdempotencyKeyClaim,
                                                  tenant.ToString(),
                                                  key.Value(),
                                                  fingerprint.Value(),
                                                  AsTimestamptz(expires_at));
    if (result.IsEmpty()) {
        throw std::runtime_error{"http_idempotency_key: занятие ключа не вернуло строки"};
    }

    const auto row = result.Front();
    if (row["mine"].As<bool>()) {
        return pdr::http::Claim{pdr::http::ClaimOutcome::kTaken, {}};
    }

    if (row["request_fingerprint"].As<std::string>() != fingerprint.Value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "idempotency_key_reused",
                           "этот ключ уже занят запросом с другим телом. Это ошибка клиента, а "
                           "не повтор: возьмите новый ключ"};
    }

    const auto state = pdr::http::ParseKeyState(row["state"].As<std::string>());
    if (!state.has_value()) {
        throw std::runtime_error{"http_idempotency_key: состояние строки не разбирается"};
    }
    if (*state == pdr::http::KeyState::kInProgress) {
        return pdr::http::Claim{pdr::http::ClaimOutcome::kInFlight, {}};
    }

    return pdr::http::Claim{pdr::http::ClaimOutcome::kReplay,
                            pdr::http::SavedAnswer{row["response_status"].As<int>(),
                                                   row["response_body"].As<std::string>()}};
}

core::Result<void> PostgresIdempotencyKeys::Complete(db::ScopedTenantContext& session,
                                                     const core::TenantId& tenant,
                                                     const pdr::http::IdempotencyKey& key,
                                                     const pdr::http::SavedAnswer& answer) {
    const auto result = session.Session().Execute(sql::kHttpIdempotencyKeyComplete,
                                                  tenant.ToString(),
                                                  key.Value(),
                                                  answer.status,
                                                  answer.body);
    if (result.RowsAffected() != 1) {
        throw std::runtime_error{"http_idempotency_key: закрывать нечего — строка занята не нами"};
    }
    return {};
}

}  // namespace pdr::infrastructure::http
