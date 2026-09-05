#include "scheduling/infrastructure/postgres_lesson_repository.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/exceptions.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

/// Одна строка участия. Порядок полей — порядок массивов в
/// db/sql/scheduling/scheduling_lesson_participants_add.sql: штатный
/// `ExecuteDecomposeBulk` раскладывает структуру по колонкам ровно так.
struct ParticipantRow final {
    core::TenantId tenant_id;
    core::LessonId lesson_id;
    core::PersonId participant_id;
};

core::PersonId AsPerson(const std::string& text, const char* column) {
    const auto parsed = core::PersonId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{std::string{"scheduling_lesson."} + column +
                                 " не идентификатор человека"};
    }
    return *parsed;
}

core::LessonId AsLesson(const std::string& text) {
    const auto parsed = core::LessonId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{"scheduling_lesson.id не идентификатор занятия"};
    }
    return *parsed;
}

core::TimeZone AsZone(const std::string& text) {
    auto zone = core::TimeZone::Parse(text);
    if (!zone.has_value()) {
        throw std::runtime_error{"scheduling_lesson.tz не имя зоны: " + text};
    }
    return *zone;
}

LessonState AsState(const std::string& text) {
    for (const auto state : kEveryLessonState) {
        if (Name(state) == text) {
            return state;
        }
    }
    throw std::runtime_error{"scheduling_lesson.state вне закрытого списка: " + text};
}

/// Собрать занятие из строки и его участников.
///
/// Домен не умеет создавать занятие в произвольном состоянии — и правильно
/// делает: `Schedule` ставит `planned`, дальше только машина переходов. Поэтому
/// восстановление идёт тем же путём, каким занятие когда-то и прошло:
/// назначается, а затем переводится в своё состояние. Второго конструктора для
/// хранилища домен не заводит, иначе машина состояний обходилась бы им.
Lesson Restore(core::TenantId tenant,
               core::LessonId id,
               core::PersonId tutor,
               std::vector<core::PersonId> participants,
               core::Instant starts_at,
               core::Instant ends_at,
               core::TimeZone zone,
               LessonState state) {
    const auto duration = std::chrono::duration_cast<Lesson::Duration>(ends_at - starts_at);
    auto lesson = Lesson::Schedule(std::move(id),
                                   std::move(tenant),
                                   std::move(tutor),
                                   std::move(participants),
                                   starts_at,
                                   duration,
                                   std::move(zone),
                                   starts_at - Lesson::Duration{1});
    if (!lesson.HasValue()) {
        throw std::runtime_error{"строка scheduling_lesson не собирается в занятие: " +
                                 lesson.Failure().Code()};
    }

    if (state == LessonState::kPlanned) {
        return lesson.Value();
    }

    for (const auto event : kEveryLessonEvent) {
        const auto moved = Transition(LessonState::kPlanned, event);
        if (!moved.HasValue() || moved.Value() != state) {
            continue;
        }
        auto changed = lesson.Value().After(event);
        if (changed.HasValue()) {
            return changed.Value();
        }
    }

    throw std::runtime_error{std::string{"состояние scheduling_lesson недостижимо из "
                                         "назначенного: "} +
                             std::string{Name(state)}};
}

/// Участники перечисленных занятий одним обращением, разложенные по занятию.
///
/// Отдельный ход, а не join к занятию: участников у занятия несколько, и join
/// размножил бы саму строку занятия по числу учеников — разбирать обратно
/// пришлось бы всё равно, только уже из повторов.
std::unordered_map<std::string, std::vector<core::PersonId>> ParticipantsOf(
    infrastructure::db::ScopedTenantContext& scope,
    const core::TenantId& tenant,
    const std::vector<core::LessonId>& ids) {
    std::unordered_map<std::string, std::vector<core::PersonId>> participants;
    if (ids.empty()) {
        return participants;
    }

    const auto people = scope.Session().Execute(sql::kSchedulingLessonParticipantsOf, tenant, ids);
    for (const auto& raw : people) {
        const auto row =
            raw.As<SchedulingLessonParticipantsOfRow>(userver::storages::postgres::kRowTag);
        participants[Filled(row.lesson_id, "lesson_id")].push_back(
            AsPerson(Filled(row.participant_id, "participant_id"), "participant_id"));
    }
    return participants;
}

/// Занятия строк вместе с их участниками.
///
/// Репетитор берётся из строки, а не от спрашивающего: у выборки участника
/// спрашивающий репетитором как раз и не является, и подставить его туда значило
/// бы собрать занятие, которого не было.
template<class Row>
std::vector<Lesson> Assemble(infrastructure::db::ScopedTenantContext& scope,
                             const core::TenantId& tenant,
                             const userver::storages::postgres::ResultSet& rows) {
    std::vector<core::LessonId> ids;
    ids.reserve(rows.Size());
    for (const auto& raw : rows) {
        const auto row = raw.template As<Row>(userver::storages::postgres::kRowTag);
        ids.push_back(AsLesson(Filled(row.id, "id")));
    }
    const auto participants = ParticipantsOf(scope, tenant, ids);

    std::vector<Lesson> found;
    found.reserve(rows.Size());
    for (const auto& raw : rows) {
        const auto row = raw.template As<Row>(userver::storages::postgres::kRowTag);
        const auto& id = Filled(row.id, "id");
        const auto seen = participants.find(id);
        found.push_back(
            Restore(tenant,
                    AsLesson(id),
                    AsPerson(Filled(row.tutor_id, "tutor_id"), "tutor_id"),
                    seen == participants.end() ? std::vector<core::PersonId>{} : seen->second,
                    AsInstant(Filled(row.starts_at, "starts_at")),
                    AsInstant(Filled(row.ends_at, "ends_at")),
                    AsZone(Filled(row.tz, "tz")),
                    AsState(Filled(row.state, "state"))));
    }
    return found;
}

}  // namespace

PostgresLessonRepository::PostgresLessonRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<Lesson> PostgresLessonRepository::Find(const core::TenantId& tenant,
                                                     const core::LessonId& id) const {
    const auto rows = scope_.Session().Execute(sql::kSchedulingLessonById, tenant, id);
    if (rows.IsEmpty()) {
        return std::nullopt;
    }

    auto found = Assemble<SchedulingLessonByIdRow>(scope_, tenant, rows);
    return std::move(found.front());
}

std::optional<Lesson> PostgresLessonRepository::FindAtSlot(const core::TenantId& tenant,
                                                           const core::PersonId& tutor,
                                                           core::Instant starts_at) const {
    const auto rows =
        scope_.Session().Execute(sql::kSchedulingLessonAtSlot, tenant, tutor, starts_at);
    if (rows.IsEmpty()) {
        return std::nullopt;
    }

    auto found = Assemble<SchedulingLessonAtSlotRow>(scope_, tenant, rows);
    return std::move(found.front());
}

std::vector<Lesson> PostgresLessonRepository::OfTutor(const core::TenantId& tenant,
                                                      const core::PersonId& tutor,
                                                      const core::TimeRange& window) const {
    const auto rows = scope_.Session().Execute(
        sql::kSchedulingLessonOfTutor, tenant, tutor, window.From(), window.To());
    return Assemble<SchedulingLessonOfTutorRow>(scope_, tenant, rows);
}

std::vector<Lesson> PostgresLessonRepository::OfParticipant(const core::TenantId& tenant,
                                                            const core::PersonId& participant,
                                                            const core::TimeRange& window) const {
    const auto rows = scope_.Session().Execute(
        sql::kSchedulingLessonOfParticipant, tenant, participant, window.From(), window.To());
    return Assemble<SchedulingLessonOfParticipantRow>(scope_, tenant, rows);
}

core::Result<void> PostgresLessonRepository::SetState(const Lesson& lesson) {
    scope_.Session().Execute(sql::kSchedulingLessonSetState,
                             lesson.Tenant(),
                             lesson.Id(),
                             std::string{Name(lesson.State())});
    return {};
}

core::Result<void> PostgresLessonRepository::Move(const Lesson& lesson) {
    try {
        scope_.Session().Execute(sql::kSchedulingLessonMove,
                                 lesson.Tenant(),
                                 lesson.Id(),
                                 lesson.StartsAt(),
                                 lesson.EndsAt());
    } catch (const userver::storages::postgres::ExclusionViolation&) {
        return core::Error{
            core::ErrorKind::kConflict, "slot_already_taken", "это время у репетитора уже занято"};
    }
    return {};
}

core::Result<void> PostgresLessonRepository::Save(const Lesson& lesson) {
    try {
        scope_.Session().Execute(sql::kSchedulingLessonSave,
                                 lesson.Tenant(),
                                 lesson.Id(),
                                 std::optional<core::SeriesId>{},
                                 lesson.Tutor(),
                                 lesson.StartsAt(),
                                 lesson.EndsAt(),
                                 lesson.Zone().Name(),
                                 std::string{Name(lesson.State())});
    } catch (const userver::storages::postgres::ExclusionViolation&) {
        return core::Error{
            core::ErrorKind::kConflict, "slot_already_taken", "это время у репетитора уже занято"};
    }

    std::vector<ParticipantRow> rows;
    rows.reserve(lesson.Participants().size());
    for (const auto& participant : lesson.Participants()) {
        rows.push_back(ParticipantRow{lesson.Tenant(), lesson.Id(), participant});
    }
    scope_.Session().ExecuteDecomposeBulk(sql::kSchedulingLessonParticipantsAdd, rows);

    return {};
}

}  // namespace pdr::scheduling
