#pragma once

#include <string_view>

#include <userver/server/http/http_method.hpp>

#include "core/idempotency.hpp"

namespace pdr::infrastructure::http {

/// Заголовок, в котором клиент присылает ключ повтора. Имя стандартное
/// (draft-ietf-httpapi-idempotency-key-header), и своего мы не заводим:
/// у половины клиентских библиотек оно уже вшито.
inline constexpr std::string_view kIdempotencyKeyHeader = "Idempotency-Key";

/// Заголовок ответа: этот ответ сохранённый, операция сейчас не выполнялась.
///
/// Клиенту он нужен затем, что «получилось» и «получилось ещё в прошлый раз» —
/// разные новости при разборе жалобы. На поведение он не влияет: тело и статус
/// те же самые, что были отданы в первый раз.
inline constexpr std::string_view kReplayedHeader = "Idempotency-Replayed";

/// Метод обращения на нашем языке.
///
/// Перевод, а не переиспользование чужого перечисления: `core` собирается без
/// userver, а «меняет ли обращение состояние» — правило домена, а не транспорта.
/// Незнакомый метод переводится в границу списка и до правил не доходит вовсе.
pdr::http::Method Translate(userver::server::http::HttpMethod method) noexcept;

/// Тот же перевод для двойника запроса, который сразу говорит на нашем языке.
constexpr pdr::http::Method Translate(pdr::http::Method method) noexcept {
    return method;
}

}  // namespace pdr::infrastructure::http
