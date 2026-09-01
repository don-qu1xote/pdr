#include "infrastructure/http/postgres_idempotency_keys.hpp"

#include <optional>
#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::infrastructure::http {
namespace {

using infrastructure::db::AsTimestamptz;

/// ЗАНЯТЬ КЛЮЧ ОДНИМ ЗАПРОСОМ, а не «сначала select, потом insert».
///
/// Между двумя запросами помещается чужой insert, и оба обращения решают, что
/// ключ свободен: ровно то двойное выполнение, ради невозможности которого всё
/// написано. Здесь всё решает первичный ключ таблицы — вставка либо прошла,
/// либо не прошла, и промежутка между этими двумя ответами не существует.
///
/// `on conflict do nothing` вместо `do update`: занятый ключ трогать нельзя, а
/// узнать про него нужно — поэтому возвращающая часть собирается из union'а
/// вставленной строки и уже лежавшей. Блокировки в процессе тут не хватило бы
/// вовсе: реплик бывает больше одной, и мьютекс в одной из них про другую не
/// знает.
const userver::storages::postgres::Query kClaim{
    "WITH taken AS ("
    "  INSERT INTO http_idempotency_key "
    "    (tenant_id, key, request_fingerprint, state, expires_at) "
    "  VALUES ($1::uuid, $2, $3, 'in_progress', $4) "
    "  ON CONFLICT (tenant_id, key) DO NOTHING "
    "  RETURNING request_fingerprint, state, response_status, response_body, true AS mine"
    ") "
    "SELECT request_fingerprint, state, response_status, response_body, mine FROM taken "
    "UNION ALL "
    "SELECT request_fingerprint, state, response_status, response_body, false AS mine "
    "  FROM http_idempotency_key "
    " WHERE key = $2 AND NOT EXISTS (SELECT 1 FROM taken)",
    userver::storages::postgres::Query::Name{"http_idempotency_key_claim"},
};

/// Записать ответ и закрыть ключ. Условие по state — не украшение: закрыть
/// чужой завершённый ключ значило бы переписать ответ, который уже отдали.
const userver::storages::postgres::Query kComplete{
    "UPDATE http_idempotency_key "
    "   SET state = 'completed', response_status = $3, response_body = $4 "
    " WHERE key = $2 AND tenant_id = $1::uuid AND state = 'in_progress'",
    userver::storages::postgres::Query::Name{"http_idempotency_key_complete"},
};

}  // namespace

core::Result<pdr::http::Claim> PostgresIdempotencyKeys::Take(
    db::ScopedTenantContext& session,
    const core::TenantId& tenant,
    const pdr::http::IdempotencyKey& key,
    const pdr::http::RequestFingerprint& fingerprint,
    core::Instant expires_at) {
    const auto result = session.Session().Execute(
        kClaim, tenant.ToString(), key.Value(), fingerprint.Value(), AsTimestamptz(expires_at));
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
    const auto result = session.Session().Execute(
        kComplete, tenant.ToString(), key.Value(), answer.status, answer.body);
    if (result.RowsAffected() != 1) {
        throw std::runtime_error{"http_idempotency_key: закрывать нечего — строка занята не нами"};
    }
    return {};
}

}  // namespace pdr::infrastructure::http
