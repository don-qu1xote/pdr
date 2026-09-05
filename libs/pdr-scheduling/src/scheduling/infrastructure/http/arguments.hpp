#pragma once

#include <string_view>

#include <userver/server/http/http_request.hpp>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "identity/contract.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "scheduling/application/list_lessons.hpp"

/// @file
/// Аргументы адреса: то, чего в разобранном теле нет и быть не может.
///
/// Читающее обращение тела не носит вовсе, и всё, что оно говорит, стоит в пути
/// и в запросе. Разбор их живёт здесь, а не в каждой ручке: три ручки читают
/// одну и ту же пару «чьё расписание и какой стороной», и три разбора этой пары
/// разошлись бы молча.
namespace pdr::scheduling::http {

inline constexpr std::string_view kWhose = "whose";
inline constexpr std::string_view kSide = "side";
inline constexpr std::string_view kFrom = "from";
inline constexpr std::string_view kTo = "to";
inline constexpr std::string_view kLesson = "lesson";

core::Result<core::PersonId> Whose(const userver::server::http::HttpRequest& request,
                                   const core::PersonId& asking);

core::Result<Side> WhichSide(const userver::server::http::HttpRequest& request);

core::Result<core::TimeRange> Window(const userver::server::http::HttpRequest& request);

core::Result<core::LessonId> WhichLesson(const userver::server::http::HttpRequest& request);

/// РЕСУРС ДЛЯ ПОЛИТИКИ: чьё расписание спрашивают и какой стороной.
///
/// Отказа здесь не бывает, и это не небрежность: политике задают вопрос ДО
/// всякой работы, а вопрос «чьё расписание» задать надо в любом случае.
/// Негодный аргумент поэтому превращается в расписание САМОГО спрашивающего —
/// вопрос про себя безопасен при любом ответе, — а отказ приходит от сценария,
/// который тот же аргумент разбирает ещё раз и уже возвращает значение.
identity::Resource ScheduleOf(const userver::server::http::HttpRequest& request,
                              const infrastructure::http::Caller& caller);

/// То же для часов работы: у них стороны не бывает — они всегда репетиторские.
identity::Resource HoursOf(const userver::server::http::HttpRequest& request,
                           const infrastructure::http::Caller& caller);

}  // namespace pdr::scheduling::http
