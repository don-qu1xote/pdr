#pragma once

#include <string>
#include <string_view>

namespace pdr::infrastructure::http {

/// Заголовок, в котором след запроса приходит и в котором уходит обратно.
inline constexpr std::string_view kRequestIdHeader = "X-Request-Id";

/// Длиннее этого значение клиента не принимается. Ограничение не про экономию:
/// след едет в каждую строку журнала, и клиент, приславший килобайт, пишет
/// килобайт в каждую строку.
inline constexpr std::size_t kRequestIdLimit = 64;

/// Годится ли то, что принёс клиент.
///
/// СЛЕД ЗАПРОСА ПРИХОДИТ СНАРУЖИ, и это единственное место, где чужая строка
/// попадает и в журнал, и в заголовок ответа. Перевод строки в ней — это лишняя
/// строка журнала, написанная клиентом, и лишний заголовок ответа, написанный
/// им же. Поэтому набор символов закрыт, а не «всё, кроме опасного»: список
/// разрешённого не устаревает, список запрещённого устаревает всегда.
bool IsUsableRequestId(std::string_view value) noexcept;

/// Свой след — ссылка трассировки userver. Не своя случайность: это ТО ЖЕ
/// значение, которое userver уже пишет в каждую строку журнала, и разбор
/// жалобы по нему сходится сам собой. Второй идентификатор рядом означал бы,
/// что человек называет один, а в логах лежит другой.
std::string TracingRequestId();

/// След этого запроса: принесённый клиентом, если годится, иначе свой.
///
/// Шаблон по типу запроса, а не свой слой поверх userver: у
/// `server::http::HttpRequest` уже есть `GetHeader`, и тестовый двойник — это
/// тип с тем же методом. Тот же приём, что у `identity::http::ReadSessionId`.
template<class Request>
std::string RequestIdOf(const Request& request) {
    const std::string_view brought = request.GetHeader(std::string{kRequestIdHeader});
    if (IsUsableRequestId(brought)) {
        return std::string{brought};
    }
    return TracingRequestId();
}

}  // namespace pdr::infrastructure::http
