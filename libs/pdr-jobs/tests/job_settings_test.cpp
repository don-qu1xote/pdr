#include "jobs/core/job_settings.hpp"

#include <chrono>
#include <optional>
#include <string>

#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::jobs::JobName;
using pdr::jobs::JobSettings;
using pdr::jobs::Outcome;
using pdr::jobs::RunRecord;

void NameIsCheckedBecauseItIsTheLockRow() {
    PDR_CHECK(JobName::Parse("notifications.reminders").has_value());
    PDR_CHECK(JobName::Parse("billing-reconcile-payments").has_value());
    PDR_CHECK(JobName::Parse("jobs_effect_cleanup").has_value());
    PDR_CHECK(JobName::Parse("a").has_value());

    // Пробел в имени блокировки — это две разные блокировки, отличающиеся
    // невидимым символом, и два воркера, каждый уверенный, что он один.
    PDR_CHECK(!JobName::Parse("notifications reminders").has_value());
    PDR_CHECK(!JobName::Parse("").has_value());
    PDR_CHECK(!JobName::Parse("Notifications.Reminders").has_value());
    PDR_CHECK(!JobName::Parse("1st-job").has_value());
    PDR_CHECK(!JobName::Parse(".reminders").has_value());
    PDR_CHECK(!JobName::Parse("reminders.").has_value());
    PDR_CHECK(!JobName::Parse("reminders\n").has_value());
    PDR_CHECK(!JobName::Parse(std::string(JobName::kMaxLength + 1, 'a')).has_value());
    PDR_CHECK(JobName::Parse(std::string(JobName::kMaxLength, 'a')).has_value());
}

void SettingsComeAsValuesAndAreChecked() {
    const auto lock = *JobName::Parse("notifications.reminders");

    const auto good = JobSettings::Compose(lock, 1h, 10min, 24h, true);
    PDR_CHECK(good.HasValue());
    PDR_CHECK(good.Value().Lock().Value() == "notifications.reminders");
    PDR_CHECK(good.Value().Period() == 1h);
    PDR_CHECK(good.Value().Attempt() == 10min);
    PDR_CHECK(good.Value().SilenceAllowed() == 24h);
    PDR_CHECK(good.Value().Enabled());
}

void SettingsThatCannotWorkAreRefused() {
    const auto lock = *JobName::Parse("notifications.reminders");

    const auto no_period = JobSettings::Compose(lock, 0s, 10min, 24h, true);
    PDR_CHECK(!no_period.HasValue());
    PDR_CHECK(no_period.Failure().Code() == "job_period_not_positive");

    const auto no_attempt = JobSettings::Compose(lock, 1h, 0s, 24h, true);
    PDR_CHECK(!no_attempt.HasValue());
    PDR_CHECK(no_attempt.Failure().Code() == "job_attempt_not_positive");

    // Прогону отведено больше, чем период: пока первый доделывает, второй уже
    // пора начинать — и они наедут друг на друга.
    const auto overlapping = JobSettings::Compose(lock, 1min, 2min, 24h, true);
    PDR_CHECK(!overlapping.HasValue());
    PDR_CHECK(overlapping.Failure().Code() == "job_attempt_over_period");

    // Тревога раньше, чем задание обязано проснуться, — ложная на каждом
    // прогоне; через неделю на такую метрику перестают смотреть.
    const auto nervous = JobSettings::Compose(lock, 1h, 10min, 30min, true);
    PDR_CHECK(!nervous.HasValue());
    PDR_CHECK(nervous.Failure().Code() == "job_silence_under_period");
}

void RunRecordKeepsDurationAndRefusesNonsense() {
    pdr::testing::FakeClock clock;
    const auto started_at = clock.Now();

    const auto record = RunRecord::Compose(started_at, started_at + 250ms, Outcome::kDone, 7, 2);
    PDR_CHECK(record.HasValue());
    PDR_CHECK(record.Value().Took() == 250ms);
    PDR_CHECK(record.Value().Produced() == 7);
    PDR_CHECK(record.Value().Repeated() == 2);
    PDR_CHECK(pdr::jobs::Name(record.Value().Result()) == "done");
    PDR_CHECK(pdr::jobs::Name(Outcome::kLockLost) == "lock_lost");
    PDR_CHECK(pdr::jobs::Name(Outcome::kTimedOut) == "timed_out");

    const auto backwards = RunRecord::Compose(started_at, started_at - 1s, Outcome::kDone, 0, 0);
    PDR_CHECK(!backwards.HasValue());
    PDR_CHECK(backwards.Failure().Code() == "job_run_ends_before_it_starts");

    const auto negative = RunRecord::Compose(started_at, started_at + 1s, Outcome::kDone, -1, 0);
    PDR_CHECK(!negative.HasValue());
    PDR_CHECK(negative.Failure().Code() == "job_run_counter_negative");
}

void SilenceIsMeasuredFromTheEndOfTheLastRun() {
    pdr::testing::FakeClock clock;
    const auto started_at = clock.Now();
    const auto record =
        RunRecord::Compose(started_at, started_at + 1s, Outcome::kDone, 1, 0).Value();

    PDR_CHECK(pdr::jobs::SilenceFor(record, started_at + 1s) == 0s);
    PDR_CHECK(pdr::jobs::SilenceFor(record, started_at + 1h) == 1h - 1s);
    // Часы, ушедшие назад, не дают отрицательного возраста: метрика с
    // отрицательным значением означает сломанный сбор, а не молодое задание.
    PDR_CHECK(pdr::jobs::SilenceFor(record, started_at) == 0s);

    const std::optional<RunRecord> last{record};
    PDR_CHECK(!pdr::jobs::HasFallenSilent(last, started_at + 1h, 24h));
    PDR_CHECK(!pdr::jobs::HasFallenSilent(last, started_at + 24h + 1s, 24h));
    PDR_CHECK(pdr::jobs::HasFallenSilent(last, started_at + 25h, 24h));

    // Не отрабатывавшее ни разу — замолчало: пустой журнал значит, что воркер
    // не поднялся, а не что всё хорошо.
    PDR_CHECK(pdr::jobs::HasFallenSilent(std::nullopt, started_at, 24h));
}

}  // namespace

int main() {
    NameIsCheckedBecauseItIsTheLockRow();
    SettingsComeAsValuesAndAreChecked();
    SettingsThatCannotWorkAreRefused();
    RunRecordKeepsDurationAndRefusesNonsense();
    SilenceIsMeasuredFromTheEndOfTheLastRun();
    return pdr::testing::Summary("jobs.settings");
}
