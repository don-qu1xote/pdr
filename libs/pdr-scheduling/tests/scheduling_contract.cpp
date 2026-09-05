#include "scheduling_contract.hpp"

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"

namespace pdr::scheduling::testing {

core::TenantId ContractGround::Tenant() {
    return pdr::testing::Numbered<core::TenantId>(1);
}

core::PersonId ContractGround::Tutor() {
    return pdr::testing::Numbered<core::PersonId>(10);
}

core::PersonId ContractGround::Student() {
    return pdr::testing::Numbered<core::PersonId>(20);
}

core::TimeZone ContractGround::Zone() {
    return core::TimeZone::Parse("Europe/Moscow").value();
}

core::Instant ContractGround::Utc(int year, unsigned month, unsigned day, unsigned hour) {
    return pdr::testing::MomentBuilder{}.Utc(year, month, day).At(hour, 0).Build();
}

core::TimeRange ContractGround::Window(core::Instant from, core::Instant to) {
    return core::TimeRange::Compose(from, to).Value();
}

Lesson ContractGround::ALesson(core::LessonId id, core::Instant starts_at) {
    return Lesson::Schedule(std::move(id),
                            Tenant(),
                            Tutor(),
                            {Student()},
                            starts_at,
                            std::chrono::minutes{60},
                            Zone(),
                            starts_at - std::chrono::hours{24})
        .Value();
}

}  // namespace pdr::scheduling::testing
