#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "identity/application/note_sensitive_access.hpp"
#include "identity/core/access_record.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

/// Фейк узкого порта: журнал, у которого можно спросить, что в нём лежит.
/// У настоящего порта такого вопроса нет и не будет — читают журнал ученик и
/// репетитор, это другой сценарий и другие права.
class FakeAccessLog final : public ports::AccessLog {
public:
    void Record(const AccessRecord& record) override {
        recorded_.push_back(record);
    }

    const std::vector<AccessRecord>& Recorded() const noexcept {
        return recorded_;
    }

private:
    std::vector<AccessRecord> recorded_;
};

const core::TenantId kTenant = Numbered<core::TenantId>(1);
const core::PersonId kStudent = Numbered<core::PersonId>(20);
const core::PersonId kStranger = Numbered<core::PersonId>(21);

TEST(AccessRecord, KindNamesAreTheWordsTheDatabaseKnows) {
    for (const auto kind :
         {ResourceKind::kRecording, ResourceKind::kTranscript, ResourceKind::kChat}) {
        const auto parsed = ParseResourceKind(Name(kind));

        ASSERT_TRUE(parsed.has_value()) << "вид «" << Name(kind) << "» не читается обратно";
        EXPECT_EQ(*parsed, kind);
    }
}

TEST(AccessRecord, UnknownKindIsNotGuessed) {
    EXPECT_FALSE(ParseResourceKind("homework").has_value());
    EXPECT_FALSE(ParseResourceKind("").has_value());
    EXPECT_FALSE(ParseResourceKind("Recording").has_value());
}

TEST(AccessRecord, WatchingOwnLeavesNoTrace) {
    const auto own = AccessRecord::Of(kTenant,
                                      kStudent,
                                      kStudent,
                                      ResourceKind::kRecording,
                                      AccessOutcome::kShown,
                                      testing::FakeClock::DefaultStart());

    ASSERT_FALSE(own.HasValue());
    EXPECT_EQ(own.Failure().Code(), "access_log_self_view");
    EXPECT_EQ(own.Failure().Kind(), core::ErrorKind::kValidation);
}

/// ГЛАВНЫЙ ТЕСТ ЖУРНАЛА. Посторонний открыл запись занятия ученика — строка
/// появилась, и в ней записано всё, о чём спросят потом: кто, чьё, что и когда.
TEST(NoteSensitiveAccess, StrangerReadingStudentDataLeavesARow) {
    FakeAccessLog log;
    const testing::FakeClock clock;
    const NoteSensitiveAccess note{log, clock};

    const auto noted =
        note.Execute(kTenant, kStranger, kStudent, ResourceKind::kRecording, AccessOutcome::kShown);

    ASSERT_TRUE(noted.HasValue());
    ASSERT_EQ(log.Recorded().size(), 1U);

    const auto& row = log.Recorded().front();
    EXPECT_EQ(row.Tenant(), kTenant);
    EXPECT_EQ(row.Actor(), kStranger);
    EXPECT_EQ(row.Subject(), kStudent);
    EXPECT_EQ(row.Kind(), ResourceKind::kRecording);
    EXPECT_EQ(row.At(), clock.Now());
}

TEST(NoteSensitiveAccess, EveryKindOfContentIsWorthARow) {
    FakeAccessLog log;
    const testing::FakeClock clock;
    const NoteSensitiveAccess note{log, clock};

    for (const auto kind :
         {ResourceKind::kRecording, ResourceKind::kTranscript, ResourceKind::kChat}) {
        ASSERT_TRUE(
            note.Execute(kTenant, kStranger, kStudent, kind, AccessOutcome::kShown).HasValue());
    }

    ASSERT_EQ(log.Recorded().size(), 3U);
    EXPECT_EQ(log.Recorded()[0].Kind(), ResourceKind::kRecording);
    EXPECT_EQ(log.Recorded()[1].Kind(), ResourceKind::kTranscript);
    EXPECT_EQ(log.Recorded()[2].Kind(), ResourceKind::kChat);
}

TEST(NoteSensitiveAccess, WatchingOwnDataDoesNotReachTheJournal) {
    FakeAccessLog log;
    const testing::FakeClock clock;
    const NoteSensitiveAccess note{log, clock};

    const auto refused =
        note.Execute(kTenant, kStudent, kStudent, ResourceKind::kChat, AccessOutcome::kShown);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "access_log_self_view");
    EXPECT_TRUE(log.Recorded().empty()) << "своё чтение оказалось в журнале";
}

/// Момент берётся из порта часов, а не у базы: иначе на вопрос «кто смотрел в
/// марте» отвечают вторые часы, а не те же, что и весь остальной сценарий.
TEST(NoteSensitiveAccess, MomentComesFromTheClockPort) {
    FakeAccessLog log;
    testing::FakeClock clock;
    const NoteSensitiveAccess note{log, clock};

    ASSERT_TRUE(
        note.Execute(kTenant, kStranger, kStudent, ResourceKind::kChat, AccessOutcome::kShown)
            .HasValue());
    const auto first = clock.Now();

    clock.Advance(std::chrono::duration_cast<core::Instant::Duration>(72h));
    ASSERT_TRUE(
        note.Execute(kTenant, kStranger, kStudent, ResourceKind::kChat, AccessOutcome::kShown)
            .HasValue());

    ASSERT_EQ(log.Recorded().size(), 2U);
    EXPECT_EQ(log.Recorded()[0].At(), first);
    EXPECT_EQ(log.Recorded()[1].At(), clock.Now());
    EXPECT_NE(log.Recorded()[0].At(), log.Recorded()[1].At());
}

}  // namespace
}  // namespace pdr::identity
