#pragma once

#include <string>
#include <string_view>

#include <userver/server/handlers/http_handler_base.hpp>

namespace pdr::scheduling_service {

/// «Процесс жив» — и больше ничего.
///
/// РАЗНЫЕ ВОПРОСЫ — РАЗНЫЕ РУЧКИ. Эта отвечает даже тогда, когда база
/// недоступна, и это её работа: перезапускать процесс из-за упавшей базы —
/// значит превратить сбой хранилища в отсутствие сервиса, а заодно потерять
/// логи и метрики того, кто мог бы объяснить, что случилось.
///
/// Готовность принимать трафик — соседняя ручка (`ReadinessHandler`), и общей
/// у них не бывает: общая отвечает «нет» на оба вопроса сразу и лишает выбора
/// того, кто её спрашивает.
///
/// Штатный `handler-ping` при этом остаётся на месте: он про то же самое, но
/// для балансера, и заменять его своим — ровно случай ADR-0013.
class HealthHandler final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-health";

    using HttpHandlerBase::HttpHandlerBase;

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context) const override;
};

}  // namespace pdr::scheduling_service
