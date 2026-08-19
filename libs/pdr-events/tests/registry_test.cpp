// Реестр событий — это каталог include/events/<контекст>/, по заголовку на тип.
// Общего заголовка «все события всех контекстов» в реестре нет: включать все
// типы сразу незачем, а сам факт такого заголовка вернул бы связь между
// контекстами. Этот тест — единственное место, где они собраны вместе, и
// собраны затем, чтобы проверить имена.
#include <set>
#include <string_view>

#include "events/event.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "testing/check.hpp"

namespace {

using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;

static_assert(pdr::events::Event<GuardianshipRevoked>);
static_assert(pdr::events::Event<LessonBooked>);

void TypeNamesAreStableAndUnique() {
    const std::set<std::string_view> types{
        GuardianshipRevoked::kType,
        LessonBooked::kType,
    };

    PDR_CHECK(types.size() == 2);
    PDR_CHECK(GuardianshipRevoked::kType == "identity.guardianship_revoked");
    PDR_CHECK(LessonBooked::kType == "scheduling.lesson_booked");
}

void TypeNameStartsWithPublishingContext() {
    PDR_CHECK(GuardianshipRevoked::kType.substr(0, 9) == "identity.");
    PDR_CHECK(LessonBooked::kType.substr(0, 11) == "scheduling.");
}

void EveryEventIsVersioned() {
    PDR_CHECK(GuardianshipRevoked::kVersion >= 1);
    PDR_CHECK(LessonBooked::kVersion >= 1);
}

}  // namespace

int main() {
    TypeNamesAreStableAndUnique();
    TypeNameStartsWithPublishingContext();
    EveryEventIsVersioned();
    return pdr::testing::Summary("events.registry");
}
