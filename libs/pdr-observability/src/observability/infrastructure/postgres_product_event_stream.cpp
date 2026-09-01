#include "observability/infrastructure/postgres_product_event_stream.hpp"

#include <string>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::observability {
namespace {

using infrastructure::db::AsTimestamptz;

/// Идентификатор строки потока. Метки в общем списке `core/types/ids.hpp` для
/// него нет намеренно: на запись потока не ссылается никто — выгрузка читает
/// её по времени, а не по имени. Первичный ключ нужен базе.
using RowId = core::StrongId<struct ProductEventRowTag>;

const userver::storages::postgres::Query kRecord{
    "INSERT INTO observability_product_event "
    "(tenant_id, id, type, version, actor_role, occurred_at, fields) "
    "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::jsonb)",
    userver::storages::postgres::Query::Name{"observability_product_event_record"},
};

/// Значение поля в JSON. Вид значения не теряется: число остаётся числом, флаг
/// — флагом, а код и ссылка — строкой. Иначе выгрузка сравнивала бы «5» с 5.
userver::formats::json::Value AsJson(const Value& value) {
    userver::formats::json::ValueBuilder built;
    switch (value.Kind()) {
        case ValueKind::kCount:
        case ValueKind::kMinutes:
        case ValueKind::kHours:
        case ValueKind::kDays:
        case ValueKind::kScore:
            built = value.Number();
            break;
        case ValueKind::kFlag:
            built = value.Yes();
            break;
        case ValueKind::kBucket:
        case ValueKind::kCode:
        case ValueKind::kReference:
            built = value.Text();
            break;
    }
    return built.ExtractValue();
}

std::string FieldsOf(const Fields& fields) {
    userver::formats::json::ValueBuilder built{userver::formats::json::Type::kObject};
    for (const auto& [name, value] : fields) {
        built[name] = AsJson(value);
    }
    return userver::formats::json::ToString(built.ExtractValue());
}

}  // namespace

PostgresProductEventStream::PostgresProductEventStream(
    infrastructure::db::ScopedTenantContext& scope,
    const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

void PostgresProductEventStream::Record(const ProductEvent& event) {
    scope_.Session().Execute(kRecord,
                             event.Tenant().ToString(),
                             ids_.Next<RowId>().ToString(),
                             event.Type(),
                             event.Version(),
                             std::string{Name(event.Actor())},
                             AsTimestamptz(event.OccurredAt()),
                             FieldsOf(event.AllFields()));
}

}  // namespace pdr::observability
