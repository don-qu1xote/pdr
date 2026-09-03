#include "infrastructure/http/postgres_idempotency_keys.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include "infrastructure/db/domain_types.hpp"

namespace pdr::infrastructure::http {
namespace {

/// Строка занятия ключа: тот же состав колонок и тот же порядок, что у
/// db/sql/http/http_idempotency_key_claim.sql.
///
/// Написана руками, а не порождена: у запроса стоит `@no-dto` — разборщик схемы
/// ВЫПОЛНЯЕТ запрос образцовыми значениями, а вставка с образцовым сроком не
/// проходит http_idempotency_key_expires_after_created (причина записана в самом
/// файле запроса). Разбор при этом всё равно идёт структурой, а не по строковому
/// имени колонки: лишняя, потерянная или переставленная колонка ломает его
/// сразу и целиком, а не на той ветке, которой не оказалось в тестах.
struct ClaimRow final {
    std::string request_fingerprint;
    std::string state;
    std::optional<std::int32_t> response_status;
    std::optional<std::string> response_body;
    bool mine{false};
};

}  // namespace

core::Result<pdr::http::Claim> PostgresIdempotencyKeys::Take(
    db::ScopedTenantContext& session,
    const core::TenantId& tenant,
    const pdr::http::IdempotencyKey& key,
    const pdr::http::RequestFingerprint& fingerprint,
    core::Instant expires_at) {
    const auto result = session.Session().Execute(
        sql::kHttpIdempotencyKeyClaim, tenant, key.Value(), fingerprint.Value(), expires_at);
    if (result.IsEmpty()) {
        throw std::runtime_error{"http_idempotency_key: занятие ключа не вернуло строки"};
    }

    const auto row = result.Front().As<ClaimRow>(userver::storages::postgres::kRowTag);
    if (row.mine) {
        return pdr::http::Claim{pdr::http::ClaimOutcome::kTaken, {}};
    }

    if (row.request_fingerprint != fingerprint.Value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "idempotency_key_reused",
                           "этот ключ уже занят запросом с другим телом. Это ошибка клиента, а "
                           "не повтор: возьмите новый ключ"};
    }

    const auto state = pdr::http::ParseKeyState(row.state);
    if (!state.has_value()) {
        throw std::runtime_error{"http_idempotency_key: состояние строки не разбирается"};
    }
    if (*state == pdr::http::KeyState::kInProgress) {
        return pdr::http::Claim{pdr::http::ClaimOutcome::kInFlight, {}};
    }

    if (!row.response_status.has_value() || !row.response_body.has_value()) {
        throw std::runtime_error{"http_idempotency_key: ключ закрыт, а ответа в строке нет"};
    }
    return pdr::http::Claim{pdr::http::ClaimOutcome::kReplay,
                            pdr::http::SavedAnswer{*row.response_status, *row.response_body}};
}

core::Result<void> PostgresIdempotencyKeys::Complete(db::ScopedTenantContext& session,
                                                     const core::TenantId& tenant,
                                                     const pdr::http::IdempotencyKey& key,
                                                     const pdr::http::SavedAnswer& answer) {
    const auto result = session.Session().Execute(
        sql::kHttpIdempotencyKeyComplete, tenant, key.Value(), answer.status, answer.body);
    if (result.RowsAffected() != 1) {
        throw std::runtime_error{"http_idempotency_key: закрывать нечего — строка занята не нами"};
    }
    return {};
}

}  // namespace pdr::infrastructure::http
