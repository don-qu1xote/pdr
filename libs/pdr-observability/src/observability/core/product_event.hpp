#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "observability/contract.hpp"

namespace pdr::observability {

/// Запись продуктового потока: тип, версия схемы, арендатор, роль, момент и поля.
///
/// Человека здесь нет и добавить его некуда — ни полем структуры, ни ключом в
/// `Fields`: `AnonymityBreach` отвергает имя, похожее на ссылку на человека, а
/// база отвергает такую строку ограничением
/// `observability_product_event_fields_are_anonymous`. Правило одно, застав его
/// нарушение, отказывают оба.
///
/// ВЕРСИЯ ЛЕЖИТ В САМОЙ ЗАПИСИ, потому что читать записи будут после смены схемы:
/// поле добавили — версия та же, старый читатель нового поля не заметит; поле
/// убрали или переименовали — версия новая, а старые записи остались в таблице
/// и читаются тем же кодом. Поэтому `Field` возвращает `optional`, а не бросает:
/// «поля не было в той версии» — обычный ответ, а не поломка.
class ProductEvent final {
public:
    static core::Result<ProductEvent> Compose(core::TenantId tenant,
                                              std::string type,
                                              int version,
                                              Role actor,
                                              core::Instant occurred_at,
                                              Fields fields);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const std::string& Type() const noexcept {
        return type_;
    }
    int Version() const noexcept {
        return version_;
    }
    Role Actor() const noexcept {
        return actor_;
    }
    core::Instant OccurredAt() const noexcept {
        return occurred_at_;
    }
    const Fields& AllFields() const noexcept {
        return fields_;
    }

    std::optional<Value> Field(std::string_view name) const;

private:
    ProductEvent(core::TenantId tenant,
                 std::string type,
                 int version,
                 Role actor,
                 core::Instant occurred_at,
                 Fields fields);

    core::TenantId tenant_;
    std::string type_;
    int version_;
    Role actor_;
    core::Instant occurred_at_;
    Fields fields_;
};

/// ЕДИНСТВЕННОЕ МЕСТО, где записаны приметы человека в поле события.
///
/// Тот же список проверяет `scripts/check_product_events.py` на реестре и база —
/// ограничением на `fields`. Три проверки, одно правило: имя вида «*_id», почта,
/// телефон, логин или имя человека в названии поля — и значение-код, оказавшееся
/// идентификатором. Ссылке (`Reference`) идентификатором быть можно: человеком
/// она не бывает по устройству типа.
std::optional<core::Error> AnonymityBreach(const Fields& fields);

}  // namespace pdr::observability
