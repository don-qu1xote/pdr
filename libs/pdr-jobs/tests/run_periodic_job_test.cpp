#include "jobs/application/run_periodic_job.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"

namespace pdr::jobs {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

/// Кластер, разделяемый воркерами: следы действий и журнал прогонов. У двух
/// процессов своя память, но эти две вещи у них общие — на то они и в базе.
class SharedLedger final : public ports::JobLedger {
public:
    bool Claim(const core::TenantId& tenant,
               const JobName& job,
               const std::string& effect_key) override {
        return claimed_.emplace(tenant.ToString(), job.Value(), effect_key).second;
    }

    std::size_t Size() const noexcept {
        return claimed_.size();
    }

private:
    std::set<std::tuple<std::string, std::string, std::string>> claimed_;
};

class SharedJournal final : public ports::JobJournal {
public:
    void Started(const JobName&, core::Instant at) override {
        last_started_ = at;
        ++runs_;
    }

    void Finished(const JobName& job, const RunRecord& record) override {
        last_.insert_or_assign(job.Value(), record);
    }

    std::optional<RunRecord> Last(const JobName& job) const override {
        const auto found = last_.find(job.Value());
        if (found == last_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::optional<core::Instant> LastStarted() const noexcept {
        return last_started_;
    }

    int Runs() const noexcept {
        return runs_;
    }

private:
    std::optional<core::Instant> last_started_;
    std::map<std::string, RunRecord> last_;
    int runs_{0};
};

/// Блокировка, которую отбирают. `hold_for` — сколько единиц работы воркер
/// успевает сделать, прежде чем она уходит к другому.
class FailingLock final : public ports::JobLock {
public:
    explicit FailingLock(int hold_for) noexcept : left_{hold_for} {}

    bool IsHeld() const override {
        if (left_ <= 0) {
            return false;
        }
        --left_;
        return true;
    }

private:
    mutable int left_;
};

class HeldLock final : public ports::JobLock {
public:
    bool IsHeld() const override {
        return true;
    }
};

class LostLock final : public ports::JobLock {
public:
    bool IsHeld() const override {
        return false;
    }
};

/// Внешний мир: то, что нельзя сделать дважды. Отправленное письмо, списанное
/// занятие, выставленный чек — здесь это просто строка в общем списке.
struct World final {
    std::vector<std::string> sent;

    int TimesSent(const std::string& key) const {
        return static_cast<int>(std::count(sent.begin(), sent.end(), key));
    }
};

/// Задание, которое каждый прогон планирует одно и то же: напомнить каждому из
/// перечисленных. Ключ детерминированный — от арендатора и повода, без «сейчас».
class Reminders final : public PeriodicJob {
public:
    Reminders(World& world, core::TenantId tenant, std::vector<std::string> keys)
        : world_{world}, tenant_{tenant}, keys_{std::move(keys)} {}

    std::vector<WorkItem> Plan(core::Instant) override {
        std::vector<WorkItem> plan;
        plan.reserve(keys_.size());
        for (const auto& key : keys_) {
            plan.push_back(WorkItem{tenant_, key});
        }
        return plan;
    }

    void Perform(const WorkItem& item) override {
        world_.sent.push_back(item.key);
    }

private:
    World& world_;
    core::TenantId tenant_;
    std::vector<std::string> keys_;
};

/// Задание, у которого каждая единица работы съедает минуту: так проверяется
/// отведённое на прогон время без единого sleep.
class SlowReminders final : public PeriodicJob {
public:
    SlowReminders(World& world,
                  pdr::testing::FakeClock& clock,
                  core::TenantId tenant,
                  int items) noexcept
        : world_{world}, clock_{clock}, tenant_{tenant}, items_{items} {}

    std::vector<WorkItem> Plan(core::Instant) override {
        std::vector<WorkItem> plan;
        for (int index = 1; index <= items_; ++index) {
            plan.push_back(WorkItem{tenant_, "lesson-" + std::to_string(index)});
        }
        return plan;
    }

    void Perform(const WorkItem& item) override {
        world_.sent.push_back(item.key);
        clock_.Advance(1min);
    }

private:
    World& world_;
    pdr::testing::FakeClock& clock_;
    core::TenantId tenant_;
    int items_;
};

class RunPeriodicJobTest : public ::testing::Test {
protected:
    JobSettings Settings() const {
        return JobSettings::Compose(
                   *JobName::Parse("notifications.reminders"), 1h, 10min, 24h, true)
            .Value();
    }

    RunPeriodicJob Runner() {
        return RunPeriodicJob{ledger_, journal_, clock_};
    }

    pdr::testing::FakeClock clock_;
    SharedLedger ledger_;
    SharedJournal journal_;

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    JobName job_{*JobName::Parse("notifications.reminders")};
};

TEST_F(RunPeriodicJobTest, OneWorkerDoesTheWholePlan) {
    World world;
    Reminders work{world, tenant_, {"lesson-1", "lesson-2", "lesson-3"}};
    HeldLock lock;

    const auto record = Runner().Execute(job_, Settings(), work, lock);

    EXPECT_EQ(record.Result(), Outcome::kDone);
    EXPECT_EQ(record.Produced(), 3);
    EXPECT_EQ(record.Repeated(), 0);
    EXPECT_EQ(world.sent.size(), 3U);
}

TEST_F(RunPeriodicJobTest, RepeatedRunSendsNothingTwice) {
    World world;
    Reminders work{world, tenant_, {"lesson-1", "lesson-2"}};
    HeldLock lock;
    auto runner = Runner();

    const auto first = runner.Execute(job_, Settings(), work, lock);
    const auto second = runner.Execute(job_, Settings(), work, lock);

    EXPECT_EQ(first.Produced(), 2);
    EXPECT_EQ(second.Produced(), 0);
    // Повтор виден в счётчике, а не только в отсутствии писем: молчаливое
    // «ничего не сделал» так же выглядит у сломанного планирования.
    EXPECT_EQ(second.Repeated(), 2);
    EXPECT_EQ(world.TimesSent("lesson-1"), 1);
    EXPECT_EQ(world.TimesSent("lesson-2"), 1);
}

TEST_F(RunPeriodicJobTest, SecondWorkerWithoutTheLockDoesNothing) {
    World world;
    Reminders first{world, tenant_, {"lesson-1", "lesson-2"}};
    HeldLock held;
    Reminders second{world, tenant_, {"lesson-1", "lesson-2"}};
    LostLock lost;

    auto runner = Runner();
    const auto winner = runner.Execute(job_, Settings(), first, held);
    const auto loser = runner.Execute(job_, Settings(), second, lost);

    EXPECT_EQ(winner.Produced(), 2);
    EXPECT_EQ(loser.Produced(), 0);
    EXPECT_EQ(loser.Result(), Outcome::kLockLost);
    EXPECT_EQ(world.sent.size(), 2U);
}

TEST_F(RunPeriodicJobTest, LockLostMidWorkDoesNotSendTwice) {
    World world;

    // Первый воркер успевает одну единицу из трёх, дальше блокировку отбирают.
    Reminders first{world, tenant_, {"lesson-1", "lesson-2", "lesson-3"}};
    FailingLock failing{1};
    auto runner = Runner();
    const auto interrupted = runner.Execute(job_, Settings(), first, failing);

    EXPECT_EQ(interrupted.Result(), Outcome::kLockLost);
    EXPECT_EQ(interrupted.Produced(), 1);

    // Второй подхватывает и строит тот же план: уже сделанное он пропускает.
    Reminders second{world, tenant_, {"lesson-1", "lesson-2", "lesson-3"}};
    HeldLock held;
    const auto continued = runner.Execute(job_, Settings(), second, held);

    EXPECT_EQ(continued.Result(), Outcome::kDone);
    EXPECT_EQ(continued.Produced(), 2);
    EXPECT_EQ(continued.Repeated(), 1);

    ASSERT_EQ(world.sent.size(), 3U);
    EXPECT_EQ(world.TimesSent("lesson-1"), 1);
    EXPECT_EQ(world.TimesSent("lesson-2"), 1);
    EXPECT_EQ(world.TimesSent("lesson-3"), 1);
    EXPECT_EQ(ledger_.Size(), 3U);
}

TEST_F(RunPeriodicJobTest, RunStopsWhenTheAllottedTimeIsOver) {
    World world;
    SlowReminders work{world, clock_, tenant_, 30};
    HeldLock lock;

    const auto record = Runner().Execute(job_, Settings(), work, lock);

    EXPECT_EQ(record.Result(), Outcome::kTimedOut);
    EXPECT_EQ(record.Produced(), 10);
    EXPECT_TRUE(record.Took() == 10min);
}

TEST_F(RunPeriodicJobTest, JournalKeepsWhenItRanAndHowLongItTook) {
    World world;
    SlowReminders work{world, clock_, tenant_, 1};
    HeldLock lock;
    const auto started_at = clock_.Now();

    Runner().Execute(job_, Settings(), work, lock);

    const auto last = journal_.Last(job_);
    ASSERT_TRUE(last.has_value());
    EXPECT_TRUE(last->StartedAt() == started_at);
    EXPECT_TRUE(last->Took() == 1min);
    EXPECT_TRUE(journal_.LastStarted() == started_at);
    EXPECT_EQ(journal_.Runs(), 1);
}

TEST_F(RunPeriodicJobTest, SilenceGrowsWhileTheWorkerIsStopped) {
    World world;
    Reminders work{world, tenant_, {"lesson-1"}};
    HeldLock lock;
    const auto settings = Settings();

    Runner().Execute(job_, settings, work, lock);
    const auto last = journal_.Last(job_);
    ASSERT_TRUE(last.has_value());

    // Воркер стоит: прогонов больше нет, а часы идут. Возраст последнего
    // прогона растёт — по нему задание и видно.
    EXPECT_TRUE(SilenceFor(*last, clock_.Now()) == 0s);
    clock_.Advance(6h);
    EXPECT_TRUE(SilenceFor(*last, clock_.Now()) == 6h);
    EXPECT_FALSE(HasFallenSilent(last, clock_.Now(), settings.SilenceAllowed()));

    clock_.Advance(19h);
    EXPECT_TRUE(SilenceFor(*last, clock_.Now()) == 25h);
    EXPECT_TRUE(HasFallenSilent(last, clock_.Now(), settings.SilenceAllowed()));
}

TEST_F(RunPeriodicJobTest, JobThatNeverRanHasFallenSilentToo) {
    EXPECT_FALSE(journal_.Last(job_).has_value());
    EXPECT_TRUE(HasFallenSilent(journal_.Last(job_), clock_.Now(), Settings().SilenceAllowed()));
}

TEST_F(RunPeriodicJobTest, EmptyPlanIsAFullRun) {
    World world;
    Reminders work{world, tenant_, {}};
    HeldLock lock;

    const auto record = Runner().Execute(job_, Settings(), work, lock);

    EXPECT_EQ(record.Result(), Outcome::kDone);
    EXPECT_EQ(record.Produced(), 0);
    EXPECT_EQ(record.Repeated(), 0);
    EXPECT_TRUE(journal_.Last(job_).has_value());
}

TEST_F(RunPeriodicJobTest, EffectsOfOneTenantDoNotBlockAnother) {
    World world;
    const auto other = Numbered<core::TenantId>(2);

    Reminders mine{world, tenant_, {"lesson-1"}};
    Reminders theirs{world, other, {"lesson-1"}};
    HeldLock lock;
    auto runner = Runner();

    const auto first = runner.Execute(job_, Settings(), mine, lock);
    const auto second = runner.Execute(job_, Settings(), theirs, lock);

    // Ключ действия совпал буквой в букву, а арендаторы разные: это две разные
    // работы, и обе обязаны состояться.
    EXPECT_EQ(first.Produced(), 1);
    EXPECT_EQ(second.Produced(), 1);
    EXPECT_EQ(second.Repeated(), 0);
    EXPECT_EQ(world.sent.size(), 2U);
}

}  // namespace
}  // namespace pdr::jobs
