/// @file
/// ИСХОДЯЩАЯ ОЧЕРЕДЬ НА НАСТОЯЩЕЙ БАЗЕ.
///
/// На фейках проверено, во что превращаются события и когда сценарий сдаётся
/// (tests/deliver_domain_events_test.cpp, tests/dispatch_outbox_test.cpp). Здесь
/// проверяется то, чего у фейка нет вовсе и быть не может:
///
///   * ДВЕ РЕПЛИКИ НЕ ЗАБИРАЮТ ОДНУ СТРОКУ. Одновременность бывает только в
///     базе: фейк живёт в одном потоке и никакого чередования не знает, поэтому
///     на нём `skip locked` был бы зелёным и без самой директивы;
///   * ОТПРАВЩИК ВИДИТ ОЧЕРЕДЬ ВСЕХ ПРАКТИК — то, ради чего у таблицы вторая
///     политика. Вторая половина того же утверждения — «а обращению человека
///     видна только своя» — проверяется НЕ ЗДЕСЬ: контур поднимает базу под
///     суперпользователем, а его не останавливает ни `enable`, ни `force`.
///     Проверять изоляцию под тем, кого политика не касается, значит проверять
///     не то; это делает `scripts/check_isolation.py` под ролью `pdr_app` на
///     живой установке;
///   * ограничения схемы: причина отказа обязана быть у сдавшейся строки и
///     обязана отсутствовать у остальных; ключ намерения уникален внутри
///     практики.
#include <cstdint>
#include <string>
#include <vector>

#include <userver/engine/task/task.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/async.hpp>

#include "builders/identifiers.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/db/unscoped_access.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "notifications/core/retry.hpp"
#include "notifications/infrastructure/postgres_outbox.hpp"
#include "notifications/infrastructure/postgres_outbox_queue.hpp"
#include "notifications_live_schema.hpp"

namespace pdr::notifications::testing {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant Noon() {
    return core::Instant::FromUnixMicros(1'800'000'000'000'000);
}

/// Пула по умолчанию на две реплики не хватает: в нём ОДНО соединение, и два
/// одновременных захвата выстроились бы в очередь на своём же пуле, не дойдя до
/// базы. Тогда «две реплики не берут одну строку» было бы зелёным и без
/// `skip locked`.
userver::storages::postgres::ClusterSettings Concurrent() {
    auto settings = userver::storages::postgres::utest::MakeDefaultClusterSettings();
    settings.pool_settings.min_size = 4;
    settings.pool_settings.max_size = 4;
    return settings;
}

class OutboxLiveTest : public ::testing::Test {
protected:
    OutboxLiveTest() {
        ApplyOutboxSchema(local_.GetCluster());
        OpenPractice(local_.GetCluster(), first_.ToString());
        OpenPractice(local_.GetCluster(), second_.ToString());
    }

    /// Положить письмо ровно тем путём, каким его кладёт обращение человека: от
    /// имени практики и внутри её транзакции.
    void Lay(const core::TenantId& tenant, const std::string& key, core::Instant due_at) {
        auto scope = tenants_.Open(tenant,
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresOutbox outbox{scope, ids_};
        const auto delivery = Delivery::Compose(tenant,
                                                Numbered<core::PersonId>(20),
                                                Channel::kPush,
                                                "scheduling.lesson_booked",
                                                key,
                                                Noon());
        ASSERT_TRUE(delivery.HasValue());
        outbox.Enqueue(delivery.Value(), due_at);
        scope.Commit();
    }

    void Withdraw(const core::TenantId& tenant, const std::string& key) {
        auto scope = tenants_.Open(tenant,
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresOutbox outbox{scope, ids_};
        outbox.Withdraw(tenant, key);
        scope.Commit();
    }

    /// Сколько строк лежит в очереди. Считается в обход политики намеренно:
    /// контур ходит в базу суперпользователем, и «под арендатором» здесь всё
    /// равно означало бы «под тем, кого политика не касается».
    std::int64_t Rows() {
        return local_.GetCluster()
            ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      "SELECT count(*)::bigint FROM notifications_outbox")
            .AsSingleRow<std::int64_t>();
    }

    std::vector<std::string> TenantsInQueue() {
        auto rows = local_.GetCluster()->Execute(
            userver::storages::postgres::ClusterHostType::kMaster,
            "SELECT tenant_id::text FROM notifications_outbox ORDER BY tenant_id");
        return rows.AsContainer<std::vector<std::string>>();
    }

    std::string StateOf(const std::string& key) {
        return local_.GetCluster()
            ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      "SELECT state FROM notifications_outbox WHERE dedup_key = $1",
                      key)
            .AsSingleRow<std::string>();
    }

    std::int32_t AttemptsOf(const std::string& key) {
        return local_.GetCluster()
            ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      "SELECT attempts FROM notifications_outbox WHERE dedup_key = $1",
                      key)
            .AsSingleRow<std::int32_t>();
    }

    RetryPolicy Policy() {
        const auto composed = RetryPolicy::Compose(3, 1min, 30s);
        EXPECT_TRUE(composed.HasValue());
        return composed.Value();
    }

    userver::storages::postgres::utest::ClusterLocal local_{Concurrent()};
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
    infrastructure::db::UnscopedAccess unscoped_{
        local_.GetCluster(), infrastructure::db::UnscopedReason::kOutboxDispatch};
    PostgresOutboxQueue queue_{unscoped_};
    infrastructure::RandomIdGenerator ids_;

    core::TenantId first_{Numbered<core::TenantId>(1)};
    core::TenantId second_{Numbered<core::TenantId>(2)};
};

}  // namespace

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: две параллельные реплики не отправляют одно
/// событие дважды.
///
/// Победитель здесь не назначен заранее, и это главное свойство проверки: оба
/// захвата запускаются и только потом ожидаются. Кто из них дойдёт до строки
/// первым — не наше дело; важно, что второй её не увидит.
UTEST_F(OutboxLiveTest, TwoReplicasNeverClaimTheSameRow) {
    Lay(first_, "one", Noon());
    Lay(first_, "two", Noon());

    PostgresOutboxQueue theirs{unscoped_};

    auto mine = userver::utils::Async("replica-one",
                                      [&] { return queue_.Claim(Noon(), Noon() + 30s, 10); });
    auto other = userver::utils::Async("replica-two",
                                       [&] { return theirs.Claim(Noon(), Noon() + 30s, 10); });

    const auto first = mine.Get();
    const auto second = other.Get();

    EXPECT_EQ(first.size() + second.size(), 2U) << "строку взяли дважды или потеряли";
    for (const auto& taken : first) {
        for (const auto& also : second) {
            EXPECT_FALSE(taken.id == also.id) << "обе реплики взяли одну и ту же строку";
        }
    }
}

/// ОТПРАВЩИК ВИДИТ ОЧЕРЕДЬ ВСЕХ ПРАКТИК — за один заход, а не по кругу.
///
/// Ради этого у таблицы и заведена вторая политика: своей практики у отправщика
/// нет, а обойти все по одной значит вернуть тот самый перебор, которого
/// очередь избегает (docs/adr/0022-outbox-dispatch-across-tenants.md).
///
/// Что при этом видно ЧЕЛОВЕКУ, здесь не проверить: контур ходит в базу
/// суперпользователем, а его не останавливает ни `enable`, ни `force`. Это
/// половина проверяется под ролью `pdr_app` — `scripts/check_isolation.py`,
/// случаи «очередь чужой практики не видна» и «объявление разбора не переживает
/// транзакцию».
UTEST_F(OutboxLiveTest, TheDispatcherSeesEveryPractice) {
    Lay(first_, "of-the-first", Noon());
    Lay(second_, "of-the-second", Noon());

    const auto claimed = queue_.Claim(Noon(), Noon() + 30s, 10);

    EXPECT_EQ(claimed.size(), 2U) << "отправщик не увидел очередь второй практики";
    EXPECT_EQ(TenantsInQueue(), (std::vector<std::string>{first_.ToString(), second_.ToString()}));
}

/// Захват СЧИТАЕТ попытку и двигает срок: строка, которую забрал умерший
/// воркер, вернётся в работу сама, когда срок выйдет. Без этого «не менее
/// одного раза» держалось бы на том, что процессы не падают.
UTEST_F(OutboxLiveTest, AClaimCountsTheAttemptAndHoldsTheRowForAWhile) {
    Lay(first_, "one", Noon());

    ASSERT_EQ(queue_.Claim(Noon(), Policy().Deadline(Noon()), 10).size(), 1U);
    EXPECT_EQ(AttemptsOf("one"), 1);

    EXPECT_TRUE(queue_.Claim(Noon() + 1s, Noon() + 31s, 10).empty())
        << "захваченную строку забрали второй раз, не дожидаясь срока";

    EXPECT_EQ(queue_.Claim(Noon() + 31s, Noon() + 61s, 10).size(), 1U)
        << "строка умершего воркера не вернулась в работу";
    EXPECT_EQ(AttemptsOf("one"), 2);
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: напоминание срабатывает в назначенное время,
/// а не раньше. Отложенная строка — это и есть весь механизм напоминаний.
UTEST_F(OutboxLiveTest, ADelayedRowWaitsForItsHour) {
    Lay(first_, "reminder", Noon() + 24h);

    EXPECT_TRUE(queue_.Claim(Noon(), Noon() + 30s, 10).empty())
        << "напоминание ушло раньше времени";
    EXPECT_TRUE(queue_.Claim(Noon() + 23h, Noon() + 23h + 30s, 10).empty());

    EXPECT_EQ(queue_.Claim(Noon() + 24h, Noon() + 24h + 30s, 10).size(), 1U)
        << "напоминание не сработало в назначенное время";
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: исчерпание попыток переводит строку в
/// gave_up — и переводит её В БАЗЕ, вместе с причиной. Что решение принято
/// верно, проверено на фейках; здесь проверено, что запись доходит.
UTEST_F(OutboxLiveTest, GivingUpIsWrittenDownWithItsReason) {
    Lay(first_, "one", Noon());
    const auto claimed = queue_.Claim(Noon(), Noon() + 30s, 10);
    ASSERT_EQ(claimed.size(), 1U);

    queue_.Settle(ports::Settlement{
        first_, claimed.front().id, OutboxState::kGaveUp, Noon() + 30s, "mail_gate_silent"});

    EXPECT_EQ(StateOf("one"), "gave_up");
    EXPECT_EQ(local_.GetCluster()
                  ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                            "SELECT failed_reason FROM notifications_outbox WHERE dedup_key = $1",
                            "one")
                  .AsSingleRow<std::string>(),
              "mail_gate_silent");

    EXPECT_TRUE(queue_.Claim(Noon() + 24h, Noon() + 24h + 30s, 10).empty())
        << "сдавшаяся строка снова пошла в работу: цикл не кончится никогда";
}

/// СДАТЬСЯ МОЛЧА НЕЛЬЗЯ — и это не договорённость, а ограничение схемы. «Не
/// ушло почему-то» разбирать некому.
UTEST_F(OutboxLiveTest, GivingUpWithoutAReasonIsRefusedByTheSchema) {
    Lay(first_, "one", Noon());
    const auto claimed = queue_.Claim(Noon(), Noon() + 30s, 10);
    ASSERT_EQ(claimed.size(), 1U);

    EXPECT_THROW(
        queue_.Settle(ports::Settlement{
            first_, claimed.front().id, OutboxState::kGaveUp, Noon() + 30s, std::string{}}),
        userver::storages::postgres::CheckViolation);
}

UTEST_F(OutboxLiveTest, ASentRowIsNotTakenAgain) {
    Lay(first_, "one", Noon());
    const auto claimed = queue_.Claim(Noon(), Noon() + 30s, 10);
    ASSERT_EQ(claimed.size(), 1U);

    queue_.Settle(ports::Settlement{
        first_, claimed.front().id, OutboxState::kSent, Noon() + 30s, std::string{}});

    EXPECT_EQ(StateOf("one"), "sent");
    EXPECT_TRUE(queue_.Claim(Noon() + 24h, Noon() + 24h + 30s, 10).empty());
}

/// ОДНО НАМЕРЕНИЕ — ОДНА СТРОКА, и повторный вызов её ДВИГАЕТ. Отсюда переносы:
/// занятие переехало — напоминание переехало вместе с ним.
UTEST_F(OutboxLiveTest, TheSameKeyMovesTheRowInsteadOfAddingOne) {
    Lay(first_, "reminder", Noon() + 24h);
    Lay(first_, "reminder", Noon() + 48h);

    EXPECT_EQ(Rows(), 1) << "тот же ключ намерения лёг второй строкой";
    EXPECT_TRUE(queue_.Claim(Noon() + 24h, Noon() + 24h + 30s, 10).empty())
        << "напоминание осталось на прежнем часе";
    EXPECT_EQ(queue_.Claim(Noon() + 48h, Noon() + 48h + 30s, 10).size(), 1U);
}

/// Ключ намерения уникален ВНУТРИ практики, а не на всю площадку: у двух
/// репетиторов бывает одно и то же занятие в один и тот же час, и вторая
/// практика не должна терять своё письмо из-за первой.
UTEST_F(OutboxLiveTest, TheSameKeyInAnotherPracticeIsAnotherLetter) {
    Lay(first_, "reminder", Noon());
    Lay(second_, "reminder", Noon());

    EXPECT_EQ(queue_.Claim(Noon(), Noon() + 30s, 10).size(), 2U);
}

/// ОТЗЫВ ЗАБИРАЕТ НЕОТПРАВЛЕННОЕ И НЕ ТРОГАЕТ УШЕДШЕЕ: «вам написали» — это
/// факт, и стирать его задним числом нечестно.
UTEST_F(OutboxLiveTest, WithdrawalTakesBackOnlyWhatHasNotGoneYet) {
    Lay(first_, "waiting", Noon() + 24h);
    Lay(first_, "gone", Noon());

    const auto claimed = queue_.Claim(Noon(), Noon() + 30s, 10);
    ASSERT_EQ(claimed.size(), 1U);
    queue_.Settle(ports::Settlement{
        first_, claimed.front().id, OutboxState::kSent, Noon() + 30s, std::string{}});

    Withdraw(first_, "waiting");
    Withdraw(first_, "gone");

    EXPECT_EQ(Rows(), 1) << "отзыв стёр уже отправленное письмо";
    EXPECT_EQ(StateOf("gone"), "sent");
}

}  // namespace pdr::notifications::testing
