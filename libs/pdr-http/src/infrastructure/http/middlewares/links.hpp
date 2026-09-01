#pragma once

#include <memory>
#include <string_view>

#include <userver/server/http/http_request.hpp>
#include <userver/server/middlewares/http_middleware_base.hpp>
#include <userver/server/request/request_context.hpp>

namespace pdr::infrastructure::http {

/// ЗВЕНЬЯ КОНВЕЙЕРА ЗАПРОСА — ШТАТНЫЕ, А НЕ СВОИ.
///
/// Конвейер берётся у userver (`server::middlewares`): у него есть то, чего у
/// самодельного не бывает. Звенья настраиваются ПО КАЖДОЙ РУЧКЕ через
/// статический конфиг, а не правкой кода; в комплекте уже идут трассировка,
/// дедлайны, распаковка, обработка исключений и учёт; порядок задаётся
/// `PipelineBuilder`. Свой конвейер при существующем штатном — ровно случай
/// ADR-0013.
///
/// ЧТО ЗВЕНЬЯ ДЕЛАЮТ. Складывают в `Prepared` то, что одинаково для всех ручек:
/// след запроса, тело, ключ повтора; и ставят заголовки безопасности. Отказы
/// они не собирают вовсе — статус выбирается в одном месте на весь проект
/// (`error_mapping.cpp`), и звено, отвечающее 400 само, развело бы форму
/// надвое.
///
/// ЧЕГО В ЗВЕНО НЕ ВЫНЕСЕНО. Открытие области арендатора
/// (`db::ScopedTenantContext`) остаётся типовой гарантией: звено, которое можно
/// забыть подключить, изоляции не обеспечивает. И занятие ключа повтора: оно
/// обязано лежать в ОДНОЙ транзакции с самой операцией, а звено работает
/// снаружи неё.

/// Заголовки безопасности НА ВСЕХ ОТВЕТАХ, включая те, до которых ручка не
/// дошла: отказ конвейера, дедлайн, необработанное исключение.
///
/// Ставятся ДО следующего звена. Ответ, собранный выше по цепочке, иначе уходил
/// бы без них — а это ровно та страница, которую покажут чужому.
class SecurityHeadersLink final : public userver::server::middlewares::HttpMiddlewareBase {
public:
    static constexpr std::string_view kName = "pdr-security-headers";

    explicit SecurityHeadersLink(const userver::server::handlers::HttpHandlerBase&) noexcept {}

private:
    void HandleRequest(userver::server::http::HttpRequest& request,
                       userver::server::request::RequestContext& context) const override;
};

/// След запроса: свой, если принесли годный, иначе штатный из трассировки.
///
/// Заголовок ответа ставится здесь же — жалобу «у меня не работает» разбирают
/// по нему, а человек может назвать только то, что видел сам.
class RequestIdLink final : public userver::server::middlewares::HttpMiddlewareBase {
public:
    static constexpr std::string_view kName = "pdr-request-id";

    explicit RequestIdLink(const userver::server::handlers::HttpHandlerBase&) noexcept {}

private:
    void HandleRequest(userver::server::http::HttpRequest& request,
                       userver::server::request::RequestContext& context) const override;
};

/// Тело запроса как есть. Сверку со схемой звено не делает: схема — знание
/// ручки, а не конвейера, и она лежит файлом рядом с ручкой.
class RequestBodyLink final : public userver::server::middlewares::HttpMiddlewareBase {
public:
    static constexpr std::string_view kName = "pdr-request-body";

    explicit RequestBodyLink(const userver::server::handlers::HttpHandlerBase&) noexcept {}

private:
    void HandleRequest(userver::server::http::HttpRequest& request,
                       userver::server::request::RequestContext& context) const override;
};

/// Ключ повтора: прочитать заголовок и разобрать его.
///
/// ЗАНЯТИЕ КЛЮЧА ЗДЕСЬ НЕ ПРОИСХОДИТ и произойти не может: строка ключа
/// обязана появляться в той же транзакции, что и сама операция, иначе повтор
/// либо выполнит её дважды, либо не выполнит никогда. Звено работает снаружи
/// транзакции, поэтому его дело — форма ключа, а не его судьба.
///
/// Обязателен ли ключ, решает форма по методу запроса: у звена нет причины
/// знать, какой метод что меняет.
class IdempotencyKeyLink final : public userver::server::middlewares::HttpMiddlewareBase {
public:
    static constexpr std::string_view kName = "pdr-idempotency-key";

    explicit IdempotencyKeyLink(const userver::server::handlers::HttpHandlerBase&) noexcept {}

private:
    void HandleRequest(userver::server::http::HttpRequest& request,
                       userver::server::request::RequestContext& context) const override;
};

using SecurityHeadersLinkFactory =
    userver::server::middlewares::SimpleHttpMiddlewareFactory<SecurityHeadersLink>;
using RequestIdLinkFactory =
    userver::server::middlewares::SimpleHttpMiddlewareFactory<RequestIdLink>;
using RequestBodyLinkFactory =
    userver::server::middlewares::SimpleHttpMiddlewareFactory<RequestBodyLink>;
using IdempotencyKeyLinkFactory =
    userver::server::middlewares::SimpleHttpMiddlewareFactory<IdempotencyKeyLink>;

}  // namespace pdr::infrastructure::http
