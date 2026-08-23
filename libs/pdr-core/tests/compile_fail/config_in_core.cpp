/// @file
/// Цель ОБЯЗАНА не собираться: значение динамического конфига читает адаптер, а
/// в домен оно приходит параметром. Цель линкуется только с pdr_core, у
/// которого userver нет, — обращение к снимку конфига остаётся неразрешённым
/// символом, и сборка падает.
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "core/money.hpp"

namespace {

const userver::dynamic_config::Key<int> kLessonsPerDay{"PDR_LESSONS_PER_DAY_LIMIT", 4};

int LimitFromCore(userver::dynamic_config::Source source) {
    return source.GetSnapshot()[kLessonsPerDay];
}

}  // namespace

int main() {
    return LimitFromCore(userver::dynamic_config::GetDefaultSource());
}
