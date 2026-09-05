/// @file
/// ГОНКА ЗА ОДИН СЛОТ — на живой базе, двумя одновременными запросами.
///
/// Доменная проверка пересечений (`scheduling::Overlaps`) смотрит на то, что
/// прочитала, а между чтением и записью помещается второе бронирование: два
/// параллельных запроса проходят её ОБА. Проверить это фейками нельзя — фейк
/// живёт в одном потоке и никакого чередования не знает, — поэтому проверка
/// живая и стоит отдельно от contract-набора: там утверждения общие для обеих
/// реализаций, а это утверждение о базе.
///
/// Ограничение `scheduling_lesson_no_overlap` (V013) даёт ровно одно занятие
/// при любом чередовании, и второй пишущий получает отказ словами, а не
/// исключением наружу.
#include <string>
#include <utility>
#include <vector>

#include <userver/engine/task/task.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/async.hpp>

#include "builders/identifiers.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/infrastructure/postgres_lesson_repository.hpp"
#include "scheduling_ground.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

/// Слот, за который спорят. Один на обоих: пересечение здесь полное, а не
/// краевое, — краевое проверяет contract-набор (`ABackToBackLessonIsAccepted`).
core::Instant Slot() {
    return ContractGround::Utc(2026, 4, 6, 12);
}

/// Пула по умолчанию на гонку не хватает: в нём ОДНО соединение, и два
/// одновременных запроса выстроились бы в очередь на своём же пуле, не дойдя до
/// базы. Тогда «гонка» была бы зелёной и без всякого ограничения в схеме.
userver::storages::postgres::ClusterSettings Concurrent() {
    auto settings = userver::storages::postgres::utest::MakeDefaultClusterSettings();
    settings.pool_settings.min_size = 4;
    settings.pool_settings.max_size = 4;
    return settings;
}

/// Одно бронирование целиком: своя область, своя транзакция, свой адаптер.
///
/// Область закрывается успехом только при успехе записи — при отказе транзакция
/// откатывается сама, и делать это руками не нужно.
core::Result<void> Book(infrastructure::db::TenantContext& tenants, core::LessonId id) {
    auto scope = tenants.Open(ContractGround::Tenant(),
                              userver::storages::postgres::ClusterHostType::kMaster,
                              userver::storages::postgres::TransactionOptions{});
    PostgresLessonRepository lessons{scope};

    auto said = lessons.Save(ContractGround::ALesson(std::move(id), Slot()));
    if (said.HasValue()) {
        scope.Commit();
    }
    return said;
}

}  // namespace

/// Победитель здесь не назначен заранее, и это главное свойство проверки: оба
/// запроса запускаются и только потом ожидаются, а кто из них дойдёт до записи
/// первым — не наше дело. Гонка с заданным порядком не проверяет ничего.
UTEST(SchedulingLessonRace, TwoBookingsOfOneSlotLeaveExactlyOneLesson) {
    userver::storages::postgres::utest::ClusterLocal local{Concurrent()};
    ApplySchedulingSchema(local.GetCluster());
    infrastructure::db::TenantContext tenants{local.GetCluster()};

    const auto first_id = pdr::testing::Numbered<core::LessonId>(801);
    const auto second_id = pdr::testing::Numbered<core::LessonId>(802);

    auto first = userver::utils::Async("first-booking", Book, std::ref(tenants), first_id);
    auto second = userver::utils::Async("second-booking", Book, std::ref(tenants), second_id);

    const auto said_first = first.Get();
    const auto said_second = second.Get();

    ASSERT_NE(said_first.HasValue(), said_second.HasValue())
        << "слот достался обоим или не достался никому";

    const auto& refused = said_first.HasValue() ? said_second : said_first;
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kConflict);
    EXPECT_EQ(refused.Failure().Code(), "slot_already_taken");
    EXPECT_FALSE(refused.Failure().Detail().empty())
        << "отказ без слов человеку ничего не объясняет";

    auto reading = tenants.Open(ContractGround::Tenant(),
                                userver::storages::postgres::ClusterHostType::kMaster,
                                userver::storages::postgres::TransactionOptions{});
    PostgresLessonRepository lessons{reading};

    const auto kept = lessons.OfTutor(ContractGround::Tenant(),
                                      ContractGround::Tutor(),
                                      ContractGround::Window(ContractGround::Utc(2026, 4, 6, 0),
                                                             ContractGround::Utc(2026, 4, 7, 0)));
    ASSERT_EQ(kept.size(), 1U) << "ограничение пропустило пересечение";

    const auto& winner = said_first.HasValue() ? first_id : second_id;
    EXPECT_TRUE(kept.front().Id() == winner) << "уцелело не то занятие, что получило успех";
    EXPECT_TRUE(kept.front().StartsAt() == Slot());
}

/// Оговорка к предыдущему: гонка отказывает НЕ ВСЕГДА.
///
/// Без неё проверка была бы зелёной и в мире, где второй запрос отклоняется
/// просто потому, что он второй. Здесь слоты соседние и не пересекаются —
/// проходят оба.
UTEST(SchedulingLessonRace, TwoBookingsOfNeighbouringSlotsBothPass) {
    userver::storages::postgres::utest::ClusterLocal local{Concurrent()};
    ApplySchedulingSchema(local.GetCluster());
    infrastructure::db::TenantContext tenants{local.GetCluster()};

    const auto booking = [&tenants](core::LessonId id, core::Instant starts_at) {
        auto scope = tenants.Open(ContractGround::Tenant(),
                                  userver::storages::postgres::ClusterHostType::kMaster,
                                  userver::storages::postgres::TransactionOptions{});
        PostgresLessonRepository lessons{scope};
        auto said = lessons.Save(ContractGround::ALesson(std::move(id), starts_at));
        if (said.HasValue()) {
            scope.Commit();
        }
        return said;
    };

    auto first = userver::utils::Async(
        "earlier-booking", booking, pdr::testing::Numbered<core::LessonId>(811), Slot());
    auto second = userver::utils::Async("later-booking",
                                        booking,
                                        pdr::testing::Numbered<core::LessonId>(812),
                                        ContractGround::Utc(2026, 4, 6, 13));

    EXPECT_TRUE(first.Get().HasValue());
    EXPECT_TRUE(second.Get().HasValue());
}

}  // namespace pdr::scheduling::testing
