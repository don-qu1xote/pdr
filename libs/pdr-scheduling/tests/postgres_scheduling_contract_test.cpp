/// @file
/// ВТОРАЯ ИНСТАНЦИАЦИЯ contract-набора расписания — против настоящих адаптеров.
///
/// Первая гоняется на фейках и не требует ничего (tests/scheduling_fakes_contract_test.cpp).
/// Эта требует живой базы: утверждения те же самые, а реализация другая — в этом
/// весь смысл, иначе подстановка Лисков остаётся обещанием.
///
/// Четыре макроса ниже переопределяются ДО включения набора: тогда те же
/// утверждения разворачиваются в UTEST-макросы и идут внутри сопрограммы.
#include <userver/utest/utest.hpp>

#define PDR_CONTRACT_SUITE_P(suite) TYPED_UTEST_SUITE_P(suite)
#define PDR_CONTRACT_TEST_P(suite, name) TYPED_UTEST_P(suite, name)
#define PDR_CONTRACT_REGISTER_P(suite, ...) REGISTER_TYPED_UTEST_SUITE_P(suite, __VA_ARGS__)
#define PDR_CONTRACT_INSTANTIATE_P(prefix, suite, types) \
    INSTANTIATE_TYPED_UTEST_SUITE_P(prefix, suite, types)

#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>

#include "builders/identifiers.hpp"
#include "fakes/fake_id_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/infrastructure/postgres_availability_repository.hpp"
#include "scheduling/infrastructure/postgres_lesson_repository.hpp"
#include "scheduling/infrastructure/postgres_recurrence_repository.hpp"
#include "scheduling_contract.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

/// Мир настоящих адаптеров: живая база, применённая схема и открытая область
/// арендатора, внутри которой и работают все три адаптера.
class PostgresWorld final {
public:
    PostgresWorld()
        : tenants_{local_.GetCluster()},
          scope_{Prepared().Open(ContractGround::Tenant(),
                                 userver::storages::postgres::ClusterHostType::kMaster,
                                 userver::storages::postgres::TransactionOptions{})},
          lessons_{scope_},
          availability_{scope_, ids_},
          series_{scope_} {}

    ports::LessonRepository& Lessons() noexcept {
        return lessons_;
    }
    ports::AvailabilityRepository& Availability() noexcept {
        return availability_;
    }
    ports::RecurrenceRepository& Series() noexcept {
        return series_;
    }

    core::LessonId NextLessonId() {
        return pdr::testing::Numbered<core::LessonId>(++issued_);
    }

    core::SeriesId SeriesId() const {
        return pdr::testing::Numbered<core::SeriesId>(700);
    }

private:
    /// Схема применяется до открытия области: внутри транзакции арендатора
    /// заводить таблицы поздно.
    infrastructure::db::TenantContext& Prepared() {
        ApplySchedulingSchema(local_.GetCluster());
        return tenants_;
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    pdr::testing::FakeIdGenerator ids_;
    infrastructure::db::TenantContext tenants_;
    infrastructure::db::ScopedTenantContext scope_;
    PostgresLessonRepository lessons_;
    PostgresAvailabilityRepository availability_;
    PostgresRecurrenceRepository series_;
    int issued_{100};
};

}  // namespace

PDR_SCHEDULING_CONTRACT(Postgres, PostgresWorld);

}  // namespace pdr::scheduling::testing
