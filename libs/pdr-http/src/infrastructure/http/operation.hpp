#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/server/http/http_request.hpp>

#include "infrastructure/http/authorized_handler.hpp"

namespace pdr::infrastructure::http {

/// Ручка, доведённая до процесса: форма запроса на настоящем запросе userver.
///
/// СТЁРТАЯ, а не типизованная: тело и ответ у каждой ручки свои — они порождены
/// из схемы, — и держать их все одним типом маршрут может только так. Область
/// арендатора и порождённые типы остались у наследника, где им и место.
using Operation = Handler<userver::server::http::HttpRequest>;

/// Компонент-операция: живёт всю жизнь процесса и отдаёт свою ручку маршруту.
///
/// Нужен затем, чтобы наследник `server::handlers::HttpHandlerBase` был ОДИН на
/// весь сервис. Маршрут находит операцию по имени из своего статического
/// конфига и зовёт `Serve`; сама операция — обычный компонент своего контекста,
/// и процесс о её внутренностях не знает.
class OperationComponent : public userver::components::ComponentBase {
public:
    using userver::components::ComponentBase::ComponentBase;

    /// Ручка, которую обслуживает этот компонент.
    virtual const Operation& Handler() const = 0;
};

}  // namespace pdr::infrastructure::http
