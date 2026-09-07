#include "scheduling/application/resolve_time_off.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"
#include "events/scheduling/time_off_applied.hpp"
#include "scheduling/application/declare_time_off.hpp"

namespace pdr::scheduling {
namespace {

void Remember(std::vector<core::PersonId>& people, const core::PersonId& person) {
    if (std::find(people.begin(), people.end(), person) == people.end()) {
        people.push_back(person);
    }
}

}  // namespace

ResolveTimeOff::ResolveTimeOff(ports::TimeOffRepository& periods,
                               ports::LessonRepository& lessons,
                               ports::LessonHistory& history,
                               ports::RecurrenceRepository& series,
                               const ports::CancellationPolicies& policies,
                               const ports::LocalDays& days,
                               const application::ports::IdGenerator& ids,
                               const application::ports::Clock& clock,
                               events::Bus& bus) noexcept
    : periods_{periods},
      lessons_{lessons},
      history_{history},
      series_{series},
      policies_{policies},
      days_{days},
      ids_{ids},
      clock_{clock},
      bus_{bus} {}

core::Result<ResolveTimeOff::Answer> ResolveTimeOff::Execute(const Request& request) const {
    if (request.decision == TimeOffDecision::kBoundary ||
        request.series == SeriesDecision::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_off_decision_unknown",
                           "такого решения по перерыву не бывает"};
    }

    const auto period = periods_.Find(request.tenant, request.period);
    if (!period.has_value()) {
        return core::Error{
            core::ErrorKind::kNotFound, "time_off_not_found", "такого перерыва здесь нет"};
    }

    const auto span = days_.Between(period->From(), period->To(), period->Zone());
    if (!span.HasValue()) {
        return span.Failure();
    }

    const auto now = clock_.Now();
    const auto affected = LessonsInside(lessons_, request.tenant, period->Person(), span.Value());

    /// СДВИГ — ЦЕЛЫМИ НЕДЕЛЯМИ, тот же, что у серий. Занятие остаётся на своём
    /// дне недели и в свой час: «вторник в 18:00» переезжает на вторник в 18:00,
    /// а не на субботу в 18:00.
    ///
    /// Считается он СУТКАМИ, и через перевод часов занятие уедет на час.
    /// Исправить это может только порт правил зоны, которого в дереве нет:
    /// `core::ZoneOffsets` приходит к домену значением, а базы IANA у ядра нет
    /// намеренно (libs/pdr-core/src/core/types/local_time.hpp).
    const auto forward = std::chrono::hours{24 * 7 * WeeksOver(*period)};

    Answer answer{static_cast<int>(affected.size()), 0, 0, 0};

    /// КОМУ ПИСАТЬ — СОБИРАЕТСЯ ПО ЛЮДЯМ, А НЕ ПО ЗАНЯТИЯМ. Ученик, потерявший
    /// четыре занятия, попадает сюда один раз, и письмо получит одно.
    std::vector<core::PersonId> tell;
    Remember(tell, period->Person());

    for (const auto& lesson : affected) {
        Remember(tell, lesson.Tutor());
        for (const auto& person : lesson.People()) {
            Remember(tell, person);
        }

        if (request.decision == TimeOffDecision::kKeep) {
            continue;
        }

        /// СТОРОНУ РЕШАЕТ САМО ЗАНЯТИЕ. Ведёт его тот, у кого перерыв, — значит
        /// отменяет репетитор, и удержания не будет никакого, какой бы ни была
        /// политика. Нет — значит на каникулах ученик, и платит он по политике,
        /// как за любую другую отмену.
        const bool by_tutor = lesson.Tutor() == period->Person();

        if (request.decision == TimeOffDecision::kCancel) {
            auto changed = [&]() -> core::Result<Lesson::Change> {
                if (by_tutor) {
                    return lesson.CancelByTutor(request.price.Currency(), request.actor, now);
                }
                const auto policy = policies_.Of(request.tenant);
                if (!policy.HasValue()) {
                    return policy.Failure();
                }
                return lesson.CancelByStudent(policy.Value(), request.price, request.actor, now);
            }();
            if (!changed.HasValue()) {
                return changed.Failure();
            }

            const auto saved = lessons_.SetState(changed.Value().lesson);
            if (!saved.HasValue()) {
                return saved.Failure();
            }
            const auto written = history_.Record(changed.Value().record);
            if (!written.HasValue()) {
                return written.Failure();
            }

            /// ПОШТУЧНОЕ СОБЫТИЕ ОСТАЁТСЯ: биллингу нужно именно поштучно, у
            /// него на каждое занятие свои деньги. А человеку поштучно не
            /// нужно — поле `time_off` и говорит подписчику, что писать об этом
            /// занятии отдельно не надо.
            bus_.Publish(events::scheduling::LessonCancelled{
                events::Envelope{request.tenant, now},
                lesson.Id(),
                lesson.Tutor(),
                lesson.Participants().front().Person(),
                request.actor,
                by_tutor ? CancelledBy::kTutor : CancelledBy::kStudent,
                changed.Value().outcome.retained,
                changed.Value().outcome.reason,
                period->Id(),
            });
            ++answer.cancelled;
            continue;
        }

        const auto policy = policies_.Of(request.tenant);
        if (!policy.HasValue()) {
            return policy.Failure();
        }
        const auto past = history_.Of(request.tenant, lesson.Id());
        auto moved = lesson.Reschedule(
            policy.Value(), request.price, request.actor, lesson.StartsAt() + forward, now, past);
        if (!moved.HasValue()) {
            return moved.Failure();
        }

        const auto saved = lessons_.Move(moved.Value().lesson);
        if (!saved.HasValue()) {
            return saved.Failure();
        }
        const auto written = history_.Record(moved.Value().record);
        if (!written.HasValue()) {
            return written.Failure();
        }

        bus_.Publish(events::scheduling::LessonRescheduled{
            events::Envelope{request.tenant, now},
            lesson.Id(),
            lesson.Tutor(),
            lesson.Participants().front().Person(),
            request.actor,
            lesson.StartsAt(),
            moved.Value().lesson.StartsAt(),
            moved.Value().outcome.retained,
            moved.Value().outcome.reason,
        });
        ++answer.postponed;
    }

    const auto series = ApplyToSeries(request, *period);
    if (!series.HasValue()) {
        return series.Failure();
    }
    answer.series = series.Value();

    const auto decided =
        periods_.Decide(request.tenant, period->Id(), request.decision, request.series, now);
    if (!decided.HasValue()) {
        return decided.Failure();
    }

    /// ОДНО СОБЫТИЕ НА ПЕРЕРЫВ — и одно письмо на человека из него. Двенадцать
    /// писем «занятие отменено» за одну секунду выглядят поломкой, а не заботой.
    bus_.Publish(events::scheduling::TimeOffApplied{
        events::Envelope{request.tenant, now},
        period->Id(),
        period->Person(),
        span.Value().From(),
        span.Value().To(),
        request.decision,
        answer.lessons,
        std::move(tell),
    });

    return answer;
}

core::Result<int> ResolveTimeOff::ApplyToSeries(const Request& request,
                                                const TimeOff& period) const {
    int touched = 0;
    for (const auto& id : series_.Of(request.tenant, period.Person())) {
        const auto series = series_.Find(request.tenant, id);
        if (!series.has_value()) {
            continue;
        }

        if (request.series == SeriesDecision::kSkip) {
            /// ПРОПУСК — ЭТО ИСКЛЮЧЕНИЯ НА ДАТЫ, попавшие в перерыв. Правило
            /// серии не трогается вовсе: после перерыва она идёт как шла, и
            /// «возобновляется» ей не приходится — она не останавливалась.
            const auto dates = OccurrenceDates(*series, period.From(), period.To());
            if (dates.empty()) {
                continue;
            }
            for (const auto& date : dates) {
                const auto skipped = series_.Record(
                    request.tenant, id, RecurrenceException{date, ExceptionKind::kCancelled});
                if (!skipped.HasValue()) {
                    return skipped.Failure();
                }
            }
            ++touched;
            continue;
        }

        /// СДВИГ — ЭТО НОВОЕ ПРАВИЛО, а не исключения. Серия, отодвинутая на
        /// две недели, доводит до конца всё, что обещала: обещали двадцать
        /// занятий — их и будет двадцать, просто последнее позже.
        ///
        /// Серию, которая до перерыва и не начиналась, двигать нечего: её
        /// вхождения в перерыв не попадают.
        if (OccurrenceDates(*series, period.From(), period.To()).empty()) {
            continue;
        }

        auto shifted =
            series->ShiftPast(period.From(), WeeksOver(period), ids_.Next<core::SeriesId>());
        if (!shifted.HasValue()) {
            return shifted.Failure();
        }

        /// Первая — всегда та же серия, её переписывают. Остальные — новые.
        auto& pieces = shifted.Value();
        const auto reshaped = series_.Reshape(pieces.front());
        if (!reshaped.HasValue()) {
            return reshaped.Failure();
        }
        for (std::size_t index = 1; index < pieces.size(); ++index) {
            const auto created = series_.Create(pieces[index]);
            if (!created.HasValue()) {
                return created.Failure();
            }
        }
        ++touched;
    }

    return touched;
}

}  // namespace pdr::scheduling
