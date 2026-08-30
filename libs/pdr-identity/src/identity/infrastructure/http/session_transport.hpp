#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "identity/core/session.hpp"

namespace pdr::identity::http {

/// ЕДИНСТВЕННОЕ МЕСТО, где идентификатор сессии достают из запроса.
///
/// Транспорт отделён от механизма, и это не украшение архитектуры. Cookie —
/// способ доставки идентификатора, а не сама сессия: мобильный клиент с cookie
/// не работает вовсе и принесёт тот же идентификатор заголовком. Проверка
/// сессии (`identity::AuthenticateSession`) про всё это не знает — на входе у
/// неё `SessionId`, и откуда он взялся, ей безразлично.
///
/// Порядок источников закрыт и записан здесь: сначала cookie, потом заголовок.
/// Именно порядок, а не «который найдётся»: иначе заголовок, подставленный
/// чужим сайтом, перебивал бы cookie собственного.
///
/// ВТОРОЙ ТРАНСПОРТ ПОКА НЕ ВКЛЮЧЁН. `kBearerHeader` объявлен и читается, но
/// выдавать идентификатор заголовком некому: мобильного клиента ещё нет. Место
/// для него оставлено, а не занято — включение будет одной строкой в списке
/// источников, а не переделкой проверки сессии.
namespace transport {

/// Имя cookie. Приставка `__Host-` — не стиль, а требование браузера: cookie с
/// таким именем принимается ТОЛЬКО с Secure, только с корневого пути и только
/// без Domain. То есть поддомен арендатора не может подсунуть свою cookie
/// основному, и проверять это не нам.
inline constexpr std::string_view kCookie = "__Host-pdr_session";

/// Заголовок для клиентов без cookie. Читается уже сейчас — чтобы «проверка не
/// зависит от транспорта» было свойством, а не обещанием.
inline constexpr std::string_view kHeader = "Authorization";

/// Приставка значения заголовка по RFC 6750.
inline constexpr std::string_view kBearer = "Bearer ";

/// Атрибуты cookie при выдаче. Все три обязательны и все три — про разное:
///
///   HttpOnly  скрипт на странице не читает её вовсе, поэтому украденный XSS-ом
///             доступ не переживает перезагрузку;
///   Secure    без TLS не отправляется — иначе идентификатор едет открытым
///             текстом по чужому Wi-Fi в кафе;
///   SameSite=Lax  чужой сайт не может послать запрос от имени человека; Strict
///             сломал бы переход по ссылке из письма, а именно так к нам
///             приходят по приглашению.
inline constexpr std::string_view kCookieAttributes = "HttpOnly; Secure; SameSite=Lax; Path=/";

}  // namespace transport

/// Достать идентификатор сессии из запроса.
///
/// Шаблон по типу запроса, а не свой интерфейс поверх userver: у
/// `server::http::HttpRequest` уже есть `GetCookie` и `GetHeader`, и заводить
/// над ними ещё один слой ради теста значило бы писать самоделку. Тестовый
/// двойник — это тип с теми же двумя методами, и больше от него ничего не
/// требуется.
template<class Request>
std::optional<SessionId> ReadSessionId(const Request& request) {
    const std::string_view from_cookie = request.GetCookie(std::string{transport::kCookie});
    if (!from_cookie.empty()) {
        const auto parsed = SessionId::Parse(from_cookie);
        return parsed ? std::optional{parsed.Value()} : std::nullopt;
    }

    const std::string_view from_header = request.GetHeader(std::string{transport::kHeader});
    if (from_header.rfind(transport::kBearer, 0) == 0) {
        const auto parsed = SessionId::Parse(from_header.substr(transport::kBearer.size()));
        return parsed ? std::optional{parsed.Value()} : std::nullopt;
    }

    return std::nullopt;
}

}  // namespace pdr::identity::http
