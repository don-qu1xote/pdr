#include "notifications/application/dispatch_outbox.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "notifications/core/outbox_entry.hpp"

namespace pdr::notifications {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

/// Очередь фейком: строки лежат в векторе, а захват ведёт себя так же, как
/// настоящий — считает попытку и двигает срок. Иначе «попытки исчерпаны»
/// проверялось бы на фейке, который попыток не считает.
class FakeQueue final : public ports::OutboxQueue {
public:
    struct Row final {
        OutboxEntry entry;
        OutboxState state{OutboxState::kPending};
        core::Instant next_attempt_at;
        std::string failed_reason;
    };

    void Lay(OutboxEntry entry, core::Instant due_at) {
        rows_.push_back(Row{std::move(entry), OutboxState::kPending, due_at, {}});
    }

    std::vector<OutboxEntry> Claim(core::Instant now, core::Instant deadline, int limit) override {
        std::vector<OutboxEntry> taken;
        for (auto& row : rows_) {
            if (static_cast<int>(taken.size()) >= limit) {
                break;
            }
            if (row.state != OutboxState::kPending || row.next_attempt_at > now) {
                continue;
            }
            ++row.entry.attempts;
            row.next_attempt_at = deadline;
            taken.push_back(row.entry);
        }
        return taken;
    }

    void Settle(const ports::Settlement& settlement) override {
        for (auto& row : rows_) {
            if (row.entry.id == settlement.id) {
                row.state = settlement.state;
                row.next_attempt_at = settlement.next_attempt_at;
                row.failed_reason = settlement.failed_reason;
                return;
            }
        }
    }

    const std::vector<Row>& Rows() const noexcept {
        return rows_;
    }

private:
    std::vector<Row> rows_;
};

/// Отправщик, которому велят, чем ответить. Настоящий отвечает почтовым узлом, и
/// узел этот в тесте недоступен по существу.
class FakeSender final : public ports::NotificationSender {
public:
    core::Result<void> Send(const Delivery& delivery) const override {
        ++sent_;
        if (refuse_) {
            return core::Error{core::ErrorKind::kConflict, "mail_gate_silent", "узел молчит"};
        }
        last_ = delivery.DedupKey();
        return {};
    }

    void Refuse(bool refuse) noexcept {
        refuse_ = refuse;
    }

    std::size_t Sent() const noexcept {
        return sent_;
    }

    const std::string& Last() const noexcept {
        return last_;
    }

private:
    mutable std::size_t sent_{0};
    mutable std::string last_;
    bool refuse_{false};
};

class FixedPolicies final : public ports::RetryPolicies {
public:
    explicit FixedPolicies(int max_attempts) : max_attempts_{max_attempts} {}

    core::Result<RetryPolicy> Current() const override {
        if (broken_) {
            return core::Error{core::ErrorKind::kValidation, "outbox_settings_broken", "негодно"};
        }
        return RetryPolicy::Compose(max_attempts_, 1min, 30s);
    }

    void Break() noexcept {
        broken_ = true;
    }

private:
    int max_attempts_;
    bool broken_{false};
};

class DispatchOutboxTest : public ::testing::Test {
protected:
    OutboxEntry Letter(int number) {
        const auto delivery = Delivery::Compose(tenant_,
                                                Numbered<core::PersonId>(20),
                                                Channel::kPush,
                                                "scheduling.lesson_booked",
                                                "key-" + std::to_string(number),
                                                clock_.Now());
        EXPECT_TRUE(delivery.HasValue());
        return OutboxEntry{Numbered<OutboxId>(number), delivery.Value(), 0};
    }

    pdr::testing::FakeClock clock_;
    FakeQueue queue_;
    FakeSender sender_;
    FixedPolicies policies_{3};

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
};

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: падение между записью занятия и отправкой не
/// теряет событие. Здесь оно проверяется в самой чистой форме, какая бывает:
/// первый проход НЕ СЛУЧИЛСЯ вовсе (процесс умер), строка осталась лежать — и
/// следующий проход её отправляет.
TEST_F(DispatchOutboxTest, ALetterSurvivesADispatcherThatNeverRan) {
    queue_.Lay(Letter(1), clock_.Now());

    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};
    const auto report = dispatching.Execute(10);

    ASSERT_TRUE(report.HasValue());
    EXPECT_EQ(report.Value().sent, 1U);
    EXPECT_EQ(queue_.Rows().front().state, OutboxState::kSent);
    EXPECT_EQ(sender_.Last(), "key-1");
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: исчерпание попыток переводит строку в
/// gave_up. Проверяются обе стороны границы: пока попытки есть — повтор с новым
/// сроком, кончились — отказ с причиной.
TEST_F(DispatchOutboxTest, ExhaustedAttemptsGiveUpWithAReason) {
    queue_.Lay(Letter(1), clock_.Now());
    sender_.Refuse(true);

    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};

    for (int pass = 1; pass < 3; ++pass) {
        clock_.SetNow(queue_.Rows().front().next_attempt_at);
        const auto report = dispatching.Execute(10);
        ASSERT_TRUE(report.HasValue());
        EXPECT_EQ(report.Value().retried, 1U) << "проход " << pass << " не отложил повтор";
        EXPECT_EQ(queue_.Rows().front().state, OutboxState::kPending);
    }

    clock_.SetNow(queue_.Rows().front().next_attempt_at);
    const auto last = dispatching.Execute(10);

    ASSERT_TRUE(last.HasValue());
    EXPECT_EQ(last.Value().gave_up, 1U);
    EXPECT_EQ(queue_.Rows().front().state, OutboxState::kGaveUp);
    EXPECT_EQ(queue_.Rows().front().failed_reason, "mail_gate_silent")
        << "сдались молча: почему письмо не дошло, узнать не по чему";
    EXPECT_EQ(sender_.Sent(), 3U) << "попыток было не столько, сколько разрешено";
}

/// Отложенная строка не берётся раньше срока. Это и есть напоминание: оно лежит
/// и ждёт, а не отправляется в момент записи на занятие.
TEST_F(DispatchOutboxTest, AReminderWaitsForItsHour) {
    queue_.Lay(Letter(1), clock_.Now() + 24h);

    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};

    const auto early = dispatching.Execute(10);
    ASSERT_TRUE(early.HasValue());
    EXPECT_EQ(early.Value().claimed, 0U) << "напоминание ушло раньше времени";

    clock_.Advance(24h);
    const auto due = dispatching.Execute(10);

    ASSERT_TRUE(due.HasValue());
    EXPECT_EQ(due.Value().sent, 1U) << "напоминание не сработало в назначенное время";
}

/// Проход берёт не всю очередь: держать соединение столько, сколько в очереди
/// накопилось, значит выесть пул первым же всплеском.
TEST_F(DispatchOutboxTest, OnePassIsNotTheWholeQueue) {
    for (int number = 1; number <= 5; ++number) {
        queue_.Lay(Letter(number), clock_.Now());
    }

    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};
    const auto report = dispatching.Execute(2);

    ASSERT_TRUE(report.HasValue());
    EXPECT_EQ(report.Value().claimed, 2U);
    EXPECT_EQ(
        std::count_if(queue_.Rows().begin(),
                      queue_.Rows().end(),
                      [](const FakeQueue::Row& row) { return row.state == OutboxState::kPending; }),
        3);
}

TEST_F(DispatchOutboxTest, BrokenSettingsStopThePassInsteadOfTheQueue) {
    queue_.Lay(Letter(1), clock_.Now());
    policies_.Break();

    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};
    const auto report = dispatching.Execute(10);

    ASSERT_FALSE(report.HasValue());
    EXPECT_EQ(report.Failure().Code(), "outbox_settings_broken");
    EXPECT_EQ(queue_.Rows().front().state, OutboxState::kPending)
        << "негодная настройка съела строку очереди";
}

TEST_F(DispatchOutboxTest, APassThatTakesNothingIsRefused) {
    const DispatchOutbox dispatching{queue_, sender_, policies_, clock_};
    const auto report = dispatching.Execute(0);

    ASSERT_FALSE(report.HasValue());
    EXPECT_EQ(report.Failure().Code(), "outbox_batch_not_positive");
}

}  // namespace
}  // namespace pdr::notifications
