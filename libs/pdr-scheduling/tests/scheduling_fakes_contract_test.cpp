/// @file
/// ПЕРВАЯ ИНСТАНЦИАЦИЯ contract-набора расписания — против фейков.
///
/// Идёт без базы и без докера, за миллисекунды. Вторая инстанциация — против
/// настоящих адаптеров на живой базе (tests/postgres_scheduling_contract_test.cpp),
/// и утверждения там ровно те же: набор один, файлов два, и расходиться им
/// нечем.
#include "builders/identifiers.hpp"
#include "fakes/fake_scheduling.hpp"
#include "scheduling_contract.hpp"

namespace pdr::scheduling::testing {
namespace {

/// Мир фейков: как создать реализацию и откуда брать идентификаторы.
class FakeWorld final {
public:
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
    FakeLessons lessons_;
    FakeAvailability availability_;
    FakeSeries series_;
    int issued_{100};
};

}  // namespace

PDR_SCHEDULING_CONTRACT(Fake, FakeWorld);

}  // namespace pdr::scheduling::testing
