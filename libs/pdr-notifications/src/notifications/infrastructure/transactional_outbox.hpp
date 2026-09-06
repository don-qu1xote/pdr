#pragma once

#include <memory>
#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "events/listeners.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/random_id_generator.hpp"

namespace pdr::notifications {

/// ТРАНЗАКЦИОННЫЙ OUTBOX: подписчик, который пишет в транзакцию издателя.
///
/// Компонент, который процесс называет соседям в статическом конфиге. Издатель
/// при этом остаётся ни при чём: расписание знает платформенный порт
/// `events::Listeners`, а не этот заголовок и не контекст оповещений вовсе.
/// Кто на самом деле сидит на шине, решает тот, кто собирает процесс.
///
/// НА КАЖДОЕ ОБРАЩЕНИЕ СВОЯ ШИНА. Общая на процесс не годится: её подписчику
/// пришлось бы откуда-то взять транзакцию, а взять её неоткуда — она у
/// обращения. Шина, подписчик и адаптер живут внутри одного объекта и умирают
/// вместе с ним, поэтому «шина пережила подписчика» здесь невыразимо.
///
/// Своей области арендатора компонент не открывает и открыть не может: сессия
/// приходит доводом. Именно поэтому строка очереди и изменение, о котором она
/// рассказывает, коммитятся вместе или не коммитятся вовсе.
class TransactionalOutbox final
    : public userver::components::ComponentBase,
      public events::Listeners<infrastructure::db::ScopedTenantContext> {
public:
    static constexpr std::string_view kName = "notifications-transactional-outbox";

    TransactionalOutbox(const userver::components::ComponentConfig& config,
                        const userver::components::ComponentContext& context);

    std::unique_ptr<events::Attachment> Attach(
        infrastructure::db::ScopedTenantContext& session) const override;

private:
    infrastructure::RandomIdGenerator ids_;
};

}  // namespace pdr::notifications
