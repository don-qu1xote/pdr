#include "jobs/application/run_periodic_job.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::jobs::JobName;
using pdr::jobs::JobSettings;
using pdr::jobs::Outcome;
using pdr::jobs::RunPeriodicJob;
using pdr::jobs::RunRecord;
using pdr::jobs::WorkItem;

/// Кластер, разделяемый воркерами: следы действий и журнал прогонов. У двух
/// процессов своя память, но эти две вещи у них общие — на то они и в базе.
class SharedLedger final : public pdr::jobs::ports::JobLedger {
public:
    bool Claim(const pdr::core::TenantId& tenant,
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

class SharedJournal final : public pdr::jobs::ports::JobJournal {
public:
    void Started(const JobName&, pdr::core::Instant at) override {
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

    std::optional<pdr::core::Instant> LastStarted() const noexcept {
        return last_started_;
    }

    int Runs() const noexcept {
        return runs_;
    }

private:
    std::optional<pdr::core::Instant> last_started_;
    std::map<std::string, RunRecord> last_;
    int runs_{0};
};

/// Блокировка, которую отбирают. `hold_for` — сколько единиц работы воркер
/// успевает сделать, прежде чем она уходит к другому.
class FailingLock final : public pdr::jobs::ports::JobLock {
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

class HeldLock final : public pdr::jobs::ports::JobLock {
public:
    bool IsHeld() const override {
        return true;
    }
};

class LostLock final : public pdr::jobs::ports::JobLock {
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
class Reminders final : public pdr::jobs::PeriodicJob {
public:
    Reminders(World& world, pdr::core::TenantId tenant, std::vector<std::string> keys)
        : world_{world}, tenant_{tenant}, keys_{std::move(keys)} {}

    std::vector<WorkItem> Plan(pdr::core::Instant) override {
        ++plans_;
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

    int Plans() const noexcept {
        return plans_;
    }

private:
    World& world_;
    pdr::core::TenantId tenant_;
    std::vector<std::string> keys_;
    int plans_{0};
};

struct Fixture final {
    pdr::testing::FakeIdGenerator ids;
    pdr::testing::FakeClock clock;
    SharedLedger ledger;
    SharedJournal journal;

    pdr::core::TenantId tenant{ids.Next<pdr::core::TenantId>()};
    JobName job{*JobName::Parse("notifications.reminders")};

    JobSettings Settings() const {
        return JobSettings::Compose(
                   *JobName::Parse("notifications.reminders"), 1h, 10min, 24h, true)
            .Value();
    }

    RunPeriodicJob Runner() {
        return RunPeriodicJob{ledger, journal, clock};
    }
};

void OneWorkerDoesTheWholePlan() {
    Fixture fixture;
    World world;
    Reminders work{world, fixture.tenant, {"lesson-1", "lesson-2", "lesson-3"}};
    HeldLock lock;

    const auto record = fixture.Runner().Execute(fixture.job, fixture.Settings(), work, lock);

    PDR_CHECK(record.Result() == Outcome::kDone);
    PDR_CHECK(record.Produced() == 3);
    PDR_CHECK(record.Repeated() == 0);
    PDR_CHECK(world.sent.size() == 3);
}

void RepeatedRunSendsNothingTwice() {
    Fixture fixture;
    World world;
    Reminders work{world, fixture.tenant, {"lesson-1", "lesson-2"}};
    HeldLock lock;
    auto runner = fixture.Runner();

    const auto first = runner.Execute(fixture.job, fixture.Settings(), work, lock);
    const auto second = runner.Execute(fixture.job, fixture.Settings(), work, lock);

    PDR_CHECK(first.Produced() == 2);
    PDR_CHECK(second.Produced() == 0);
    // Повтор виден в счётчике, а не только в отсутствии писем: молчаливое
    // «ничего не сделал» так же выглядит у сломанного планирования.
    PDR_CHECK(second.Repeated() == 2);
    PDR_CHECK(world.TimesSent("lesson-1") == 1);
    PDR_CHECK(world.TimesSent("lesson-2") == 1);
}

void SecondWorkerWithoutTheLockDoesNothing() {
    Fixture fixture;
    World world;

    Reminders first{world, fixture.tenant, {"lesson-1", "lesson-2"}};
    HeldLock held;
    Reminders second{world, fixture.tenant, {"lesson-1", "lesson-2"}};
    LostLock lost;

    auto runner = fixture.Runner();
    const auto winner = runner.Execute(fixture.job, fixture.Settings(), first, held);
    const auto loser = runner.Execute(fixture.job, fixture.Settings(), second, lost);

    PDR_CHECK(winner.Produced() == 2);
    PDR_CHECK(loser.Produced() == 0);
    PDR_CHECK(loser.Result() == Outcome::kLockLost);
    PDR_CHECK(world.sent.size() == 2);
}

void LockLostMidWorkDoesNotSendTwice() {
    Fixture fixture;
    World world;

    // Первый воркер успевает одну единицу из трёх, дальше блокировку отбирают.
    Reminders first{world, fixture.tenant, {"lesson-1", "lesson-2", "lesson-3"}};
    FailingLock failing{1};
    auto runner = fixture.Runner();
    const auto interrupted = runner.Execute(fixture.job, fixture.Settings(), first, failing);

    PDR_CHECK(interrupted.Result() == Outcome::kLockLost);
    PDR_CHECK(interrupted.Produced() == 1);

    // Второй подхватывает и строит тот же план: уже сделанное он пропускает.
    Reminders second{world, fixture.tenant, {"lesson-1", "lesson-2", "lesson-3"}};
    HeldLock held;
    const auto continued = runner.Execute(fixture.job, fixture.Settings(), second, held);

    PDR_CHECK(continued.Result() == Outcome::kDone);
    PDR_CHECK(continued.Produced() == 2);
    PDR_CHECK(continued.Repeated() == 1);

    PDR_CHECK(world.sent.size() == 3);
    PDR_CHECK(world.TimesSent("lesson-1") == 1);
    PDR_CHECK(world.TimesSent("lesson-2") == 1);
    PDR_CHECK(world.TimesSent("lesson-3") == 1);
    PDR_CHECK(fixture.ledger.Size() == 3);
}

void RunStopsWhenTheAllottedTimeIsOver() {
    Fixture fixture;
    World world;

    // Каждая единица работы съедает минуту, а прогону отведено десять.
    class SlowReminders final : public pdr::jobs::PeriodicJob {
    public:
        SlowReminders(World& world,
                      pdr::testing::FakeClock& clock,
                      pdr::core::TenantId tenant) noexcept
            : world_{world}, clock_{clock}, tenant_{tenant} {}

        std::vector<WorkItem> Plan(pdr::core::Instant) override {
            std::vector<WorkItem> plan;
            for (int index = 1; index <= 30; ++index) {
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
        pdr::core::TenantId tenant_;
    };

    SlowReminders work{world, fixture.clock, fixture.tenant};
    HeldLock lock;

    const auto record = fixture.Runner().Execute(fixture.job, fixture.Settings(), work, lock);

    PDR_CHECK(record.Result() == Outcome::kTimedOut);
    PDR_CHECK(record.Produced() == 10);
    PDR_CHECK(record.Took() == 10min);
}

void JournalKeepsWhenItRanAndHowLongItTook() {
    Fixture fixture;
    World world;

    class Slow final : public pdr::jobs::PeriodicJob {
    public:
        Slow(pdr::testing::FakeClock& clock, pdr::core::TenantId tenant) noexcept
            : clock_{clock}, tenant_{tenant} {}

        std::vector<WorkItem> Plan(pdr::core::Instant) override {
            return {WorkItem{tenant_, "lesson-1"}};
        }

        void Perform(const WorkItem&) override {
            clock_.Advance(3s);
        }

    private:
        pdr::testing::FakeClock& clock_;
        pdr::core::TenantId tenant_;
    };

    Slow work{fixture.clock, fixture.tenant};
    HeldLock lock;
    const auto started_at = fixture.clock.Now();

    fixture.Runner().Execute(fixture.job, fixture.Settings(), work, lock);

    const auto last = fixture.journal.Last(fixture.job);
    PDR_CHECK(last.has_value());
    PDR_CHECK(last->StartedAt() == started_at);
    PDR_CHECK(last->Took() == 3s);
    PDR_CHECK(fixture.journal.LastStarted() == started_at);
    PDR_CHECK(fixture.journal.Runs() == 1);
}

void SilenceGrowsWhileTheWorkerIsStopped() {
    Fixture fixture;
    World world;
    Reminders work{world, fixture.tenant, {"lesson-1"}};
    HeldLock lock;
    const auto settings = fixture.Settings();

    fixture.Runner().Execute(fixture.job, settings, work, lock);
    const auto last = fixture.journal.Last(fixture.job);
    PDR_CHECK(last.has_value());

    // Воркер стоит: прогонов больше нет, а часы идут. Возраст последнего
    // прогона растёт — по нему задание и видно.
    PDR_CHECK(pdr::jobs::SilenceFor(*last, fixture.clock.Now()) == 0s);
    fixture.clock.Advance(6h);
    PDR_CHECK(pdr::jobs::SilenceFor(*last, fixture.clock.Now()) == 6h);
    PDR_CHECK(!pdr::jobs::HasFallenSilent(last, fixture.clock.Now(), settings.SilenceAllowed()));

    fixture.clock.Advance(19h);
    PDR_CHECK(pdr::jobs::SilenceFor(*last, fixture.clock.Now()) == 25h);
    PDR_CHECK(pdr::jobs::HasFallenSilent(last, fixture.clock.Now(), settings.SilenceAllowed()));
}

void JobThatNeverRanHasFallenSilentToo() {
    Fixture fixture;
    const auto settings = fixture.Settings();

    PDR_CHECK(!fixture.journal.Last(fixture.job).has_value());
    PDR_CHECK(pdr::jobs::HasFallenSilent(
        fixture.journal.Last(fixture.job), fixture.clock.Now(), settings.SilenceAllowed()));
}

void EmptyPlanIsAFullRun() {
    Fixture fixture;
    World world;
    Reminders work{world, fixture.tenant, {}};
    HeldLock lock;

    const auto record = fixture.Runner().Execute(fixture.job, fixture.Settings(), work, lock);

    PDR_CHECK(record.Result() == Outcome::kDone);
    PDR_CHECK(record.Produced() == 0);
    PDR_CHECK(record.Repeated() == 0);
    PDR_CHECK(fixture.journal.Last(fixture.job).has_value());
}

void EffectsOfOneTenantDoNotBlockAnother() {
    Fixture fixture;
    World world;
    const auto other = fixture.ids.Next<pdr::core::TenantId>();

    Reminders mine{world, fixture.tenant, {"lesson-1"}};
    Reminders theirs{world, other, {"lesson-1"}};
    HeldLock lock;
    auto runner = fixture.Runner();

    const auto first = runner.Execute(fixture.job, fixture.Settings(), mine, lock);
    const auto second = runner.Execute(fixture.job, fixture.Settings(), theirs, lock);

    // Ключ действия совпал буквой в букву, а арендаторы разные: это две разные
    // работы, и обе обязаны состояться.
    PDR_CHECK(first.Produced() == 1);
    PDR_CHECK(second.Produced() == 1);
    PDR_CHECK(second.Repeated() == 0);
    PDR_CHECK(world.sent.size() == 2);
}

}  // namespace

int main() {
    OneWorkerDoesTheWholePlan();
    RepeatedRunSendsNothingTwice();
    SecondWorkerWithoutTheLockDoesNothing();
    LockLostMidWorkDoesNotSendTwice();
    RunStopsWhenTheAllottedTimeIsOver();
    JournalKeepsWhenItRanAndHowLongItTook();
    SilenceGrowsWhileTheWorkerIsStopped();
    JobThatNeverRanHasFallenSilentToo();
    EmptyPlanIsAFullRun();
    EffectsOfOneTenantDoNotBlockAnother();
    return pdr::testing::Summary("jobs.run_periodic");
}
