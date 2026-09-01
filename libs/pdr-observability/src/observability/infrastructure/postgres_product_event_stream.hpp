#pragma once

#include "application/ports/id_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "observability/application/ports/product_event_stream.hpp"

namespace pdr::observability {

/// Запись продуктового потока в `observability_product_event`.
///
/// Поля кладутся в `fields` ШТАТНЫМ `formats::json`, а не сборкой строки:
/// собранная руками строка ломается на первой же кавычке в значении, и ломается
/// молча — база примет её как текст, а `jsonb` не примет вовсе.
///
/// Обезличивание проверяется дважды и в разных местах: домен отказывает при
/// сборке записи (`AnonymityBreach`), база — ограничением
/// `observability_product_event_fields_are_anonymous`. Здесь не проверяется
/// ничего: адаптер кладёт то, что ему дали, и это правильно — правило одно, и
/// мест его применения два, а не три.
class PostgresProductEventStream final : public ports::ProductEventStream {
public:
    PostgresProductEventStream(infrastructure::db::ScopedTenantContext& scope,
                               const application::ports::IdGenerator& ids) noexcept;

    void Record(const ProductEvent& event) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::observability
