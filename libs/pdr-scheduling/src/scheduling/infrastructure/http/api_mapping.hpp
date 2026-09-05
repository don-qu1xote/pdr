#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <pdr/api/openapi.hpp>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "scheduling/application/list_lessons.hpp"
#include "scheduling/core/availability.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/recurrence.hpp"

/// @file
/// Перевод между типами контракта и доменом — В ОДНОМ МЕСТЕ.
///
/// Типы контракта порождены из docs/api/openapi.yaml и ничего не знают про
/// домен; доменные типы собираются только своими `Compose` и ничего не знают
/// про JSON. Между ними нужен перевод, и он ровно один: шесть ручек с
/// собственным переводом дали бы шесть представлений о том, что такое «время на
/// часах», и разошлись бы они молча.
///
/// Отказ здесь — обычное значение: `2026-13-45` проходит образец схемы и не
/// проходит календарь, и сказать об этом должен домен, а не образец.
namespace pdr::scheduling::http {

core::PersonId AsPerson(const boost::uuids::uuid& value) noexcept;
core::LessonId AsLesson(const boost::uuids::uuid& value) noexcept;

boost::uuids::uuid AsUuid(const core::IdBytes& bytes) noexcept;

core::Result<core::TimeZone> AsZone(const std::string& name);
core::Result<core::Date> AsDate(const std::string& text);
core::Result<core::LocalTime> AsClock(const std::string& text);

std::string FormatDate(const core::Date& date);
std::string FormatClock(core::LocalTime time);

Side AsSide(api::LessonSide side) noexcept;

api::Lesson AsAnswer(const Lesson& lesson);
api::Lessons AsAnswer(const std::vector<Lesson>& lessons);
api::Series AsAnswer(const RecurrenceSeries& series);
api::Availability AsAnswer(const Availability& availability);

core::Result<Availability> AsDomain(const api::Availability& availability);

/// Аргумент адреса, который не разобрался. Имя аргумента — в тексте: «bad
/// request» без имени отправляет человека угадывать.
core::Error BadArgument(std::string_view name);

}  // namespace pdr::scheduling::http
