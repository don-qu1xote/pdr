#include "jobs/core/job_settings.hpp"

#include <chrono>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "fakes/fake_clock.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"

namespace pdr::jobs {
namespace {

using namespace std::chrono_literals;

TEST(JobName, IsCheckedBecauseItIsTheLockRow) {
    EXPECT_TRUE(JobName::Parse("notifications.reminders").has_value());
    EXPECT_TRUE(JobName::Parse("billing-reconcile-payments").has_value());
    EXPECT_TRUE(JobName::Parse("jobs_effect_cleanup").has_value());
    EXPECT_TRUE(JobName::Parse("a").has_value());
    EXPECT_TRUE(JobName::Parse(std::string(JobName::kMaxLength, 'a')).has_value());

    // Пробел в имени блокировки — это две разные блокировки, отличающиеся
    // невидимым символом, и два воркера, каждый уверенный, что он один.
    EXPECT_FALSE(JobName::Parse("notifications reminders").has_value());
    EXPECT_FALSE(JobName::Parse("").has_value());
    EXPECT_FALSE(JobName::Parse("Notifications.Reminders").has_value());
    EXPECT_FALSE(JobName::Parse("1st-job").has_value());
    EXPECT_FALSE(JobName::Parse(".reminders").has_value());
    EXPECT_FALSE(JobName::Parse("reminders.").has_value());
    EXPECT_FALSE(JobName::Parse("reminders\n").has_value());
    EXPECT_FALSE(JobName::Parse(std::string(JobName::kMaxLength + 1, 'a')).has_value());
}

TEST(JobSettings, ComeAsValuesAndAreChecked) {
    const auto lock = *JobName::Parse("notifications.reminders");

    const auto good = JobSettings::Compose(lock, 1h, 10min, 24h, true);
    ASSERT_TRUE(good.HasValue());
    EXPECT_EQ(good.Value().Lock().Value(), "notifications.reminders");
    EXPECT_TRUE(good.Value().Period() == 1h);
    EXPECT_TRUE(good.Value().Attempt() == 10min);
    EXPECT_TRUE(good.Value().SilenceAllowed() == 24h);
    EXPECT_TRUE(good.Value().Enabled());
}

TEST(JobSettings, ThatCannotWorkAreRefused) {
    const auto lock = *JobName::Parse("notifications.reminders");

    const auto no_period = JobSettings::Compose(lock, 0s, 10min, 24h, true);
    ASSERT_FALSE(no_period.HasValue());
    EXPECT_EQ(no_period.Failure().Code(), "job_period_not_positive");

    const auto no_attempt = JobSettings::Compose(lock, 1h, 0s, 24h, true);
    ASSERT_FALSE(no_attempt.HasValue());
    EXPECT_EQ(no_attempt.Failure().Code(), "job_attempt_not_positive");

    // Прогону отведено больше, чем период: пока первый доделывает, второй уже
    // пора начинать — и они наедут друг на друга.
    const auto overlapping = JobSettings::Compose(lock, 1min, 2min, 24h, true);
    ASSERT_FALSE(overlapping.HasValue());
    EXPECT_EQ(overlapping.Failure().Code(), "job_attempt_over_period");

    // Тревога раньше, чем задание обязано проснуться, — ложная на каждом
    // прогоне; через неделю на такую метрику перестают смотреть.
    const auto nervous = JobSettings::Compose(lock, 1h, 10min, 30min, true);
    ASSERT_FALSE(nervous.HasValue());
    EXPECT_EQ(nervous.Failure().Code(), "job_silence_under_period");
}

TEST(RunRecord, KeepsDurationAndRefusesNonsense) {
    pdr::testing::FakeClock clock;
    const auto started_at = clock.Now();

    const auto record = RunRecord::Compose(started_at, started_at + 250ms, Outcome::kDone, 7, 2);
    ASSERT_TRUE(record.HasValue());
    EXPECT_TRUE(record.Value().Took() == 250ms);
    EXPECT_EQ(record.Value().Produced(), 7);
    EXPECT_EQ(record.Value().Repeated(), 2);
    EXPECT_EQ(Name(record.Value().Result()), "done");
    EXPECT_EQ(Name(Outcome::kLockLost), "lock_lost");
    EXPECT_EQ(Name(Outcome::kTimedOut), "timed_out");

    const auto backwards = RunRecord::Compose(started_at, started_at - 1s, Outcome::kDone, 0, 0);
    ASSERT_FALSE(backwards.HasValue());
    EXPECT_EQ(backwards.Failure().Code(), "job_run_ends_before_it_starts");

    const auto negative = RunRecord::Compose(started_at, started_at + 1s, Outcome::kDone, -1, 0);
    ASSERT_FALSE(negative.HasValue());
    EXPECT_EQ(negative.Failure().Code(), "job_run_counter_negative");
}

TEST(RunRecord, SilenceIsMeasuredFromTheEndOfTheLastRun) {
    pdr::testing::FakeClock clock;
    const auto started_at = clock.Now();
    const auto record =
        RunRecord::Compose(started_at, started_at + 1s, Outcome::kDone, 1, 0).Value();

    EXPECT_TRUE(SilenceFor(record, started_at + 1s) == 0s);
    EXPECT_TRUE(SilenceFor(record, started_at + 1h) == 1h - 1s);
    // Часы, ушедшие назад, не дают отрицательного возраста: метрика с
    // отрицательным значением означает сломанный сбор, а не молодое задание.
    EXPECT_TRUE(SilenceFor(record, started_at) == 0s);

    const std::optional<RunRecord> last{record};
    EXPECT_FALSE(HasFallenSilent(last, started_at + 1h, 24h));
    EXPECT_FALSE(HasFallenSilent(last, started_at + 24h + 1s, 24h));
    EXPECT_TRUE(HasFallenSilent(last, started_at + 25h, 24h));

    // Не отрабатывавшее ни разу — замолчало: пустой журнал значит, что воркер
    // не поднялся, а не что всё хорошо.
    EXPECT_TRUE(HasFallenSilent(std::nullopt, started_at, 24h));
}

}  // namespace
}  // namespace pdr::jobs
