#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/sha256_digests.hpp"

namespace pdr::scheduling::http {

/// ОПОЗНАНИЕ ПО ССЫЛКЕ НА КАЛЕНДАРЬ: второй адаптер порта `Callers`.
///
/// Порт живёт в HTTP-форме, а знание о том, где лежит удостоверение, — здесь.
/// У сессии оно в cookie, у подписки — В САМОМ АДРЕСЕ, и это не послабление:
/// ленту забирает чужая программа, которая умеет сходить по ссылке и больше
/// ничего. Ни cookie завести, ни заголовок поставить она не может.
///
/// ФОРМА ПРИ ЭТОМ ТА ЖЕ. Политику после опознания спрашивают так же, как у
/// любой другой ручки: подписка открывает СВОЁ расписание, и решает это
/// identity, а не эта ручка.
///
/// В БАЗУ УХОДИТ ОТПЕЧАТОК ПРИШЕДШЕГО, а не сам секрет: сравнить он позволяет,
/// восстановить себя — нет. Утёкшая копия базы не даёт ни одной работающей
/// ссылки.
class FeedCallersComponent final : public userver::components::ComponentBase,
                                   public infrastructure::http::Callers {
public:
    static constexpr std::string_view kName = "scheduling-feed-callers";

    /// Имена аргументов адреса `/cabinet/{tenant}/calendar/{secret}/schedule.ics`.
    ///
    /// АРЕНДАТОР В АДРЕСЕ, И ЭТО НЕ УТЕЧКА. Тем же способом устроен вход
    /// (`/cabinet/{tenant}/sign-in`): кабинет в адресе не секрет, секрет — то,
    /// что рядом с ним. Зато поиск подписки идёт уже внутри объявленного
    /// арендатора, и сквозной политики не нужно ни одной.
    static constexpr std::string_view kTenantArgument = "tenant";
    static constexpr std::string_view kSecretArgument = "secret";

    FeedCallersComponent(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

    infrastructure::http::CredentialSource Where() const override;

    core::Result<infrastructure::http::Caller> Identify(
        std::string_view cookie,
        std::string_view header,
        const infrastructure::http::PathArguments& path) const override;

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::Sha256Digests digests_;
};

}  // namespace pdr::scheduling::http
