#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/userver_clock.hpp"

namespace pdr::identity {

/// ОПОЗНАНИЕ ПРИШЕДШЕГО: адаптер порта `infrastructure::http::Callers`.
///
/// Порт живёт в HTTP-форме, а знание о том, где лежит удостоверение и чем оно
/// истекает, — здесь: это identity, а не транспорт. Форма спрашивает «кто это»
/// и получает арендатора с человеком; про cookie, заголовок и сроки она не
/// знает ничего.
///
/// Арендатор берётся ИЗ САМОГО ИДЕНТИФИКАТОРА сессии, а не из адреса: сессия
/// выдана в конкретном кабинете, и предъявленная в чужом она чужая. Поэтому
/// область открывается по арендатору из идентификатора — и политика базы
/// отсечёт всё остальное сама.
class CallersComponent final : public userver::components::ComponentBase,
                               public infrastructure::http::Callers {
public:
    static constexpr std::string_view kName = "identity-callers";

    CallersComponent(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

    infrastructure::http::CredentialSource Where() const override;

    /// Опознание идёт НА МАСТЕР, хотя ничего не пишет.
    ///
    /// Сессию завёл предыдущий запрос — вход, — и читают её здесь сразу после
    /// записи. Реплика отстаёт на доли секунды, и этого хватает, чтобы человек
    /// вошёл и на следующем же запросе получил «сессии нет».
    ///
    /// Транзакция при этом читающая: опознание не пишет ничего, и пусть база
    /// откажет тому, кто однажды допишет сюда «отметим последний вход».
    core::Result<infrastructure::http::Caller> Identify(std::string_view cookie,
                                                        std::string_view header) const override;

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::UserverClock clock_;
};

}  // namespace pdr::identity
