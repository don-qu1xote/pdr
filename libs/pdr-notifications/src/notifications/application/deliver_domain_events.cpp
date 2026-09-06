#include "notifications/application/deliver_domain_events.hpp"

#include <string>

#include "events/identity/capabilities_widened.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/notifications/reminder_day_before.hpp"
#include "events/notifications/reminder_hour_before.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"

namespace pdr::notifications {
namespace {

using CapabilitiesWidened = pdr::events::identity::CapabilitiesWidened;
using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;
using LessonCancelled = pdr::events::scheduling::LessonCancelled;
using LessonRescheduled = pdr::events::scheduling::LessonRescheduled;
using ReminderDayBefore = pdr::events::notifications::ReminderDayBefore;
using ReminderHourBefore = pdr::events::notifications::ReminderHourBefore;
using WardActedAlone = pdr::events::identity::WardActedAlone;

/// КЛЮЧ СЛУЧИВШЕГОСЯ: повод, о ком речь, кому пишем и КОГДА это произошло.
///
/// Момент в ключе — не «сейчас», а время самого события, и разница здесь вся.
/// «Сейчас» новое на каждом проходе, и ключ перестаёт быть ключом. Время
/// события одно и то же, сколько бы раз обработчик ни отработал, — и при этом
/// разное у двух настоящих событий. Опеку отозвали, вернули и отозвали снова:
/// это два письма, а не одно, и без момента второе потерялось бы молча.
std::string Happened(std::string_view reason,
                     const std::string& about,
                     const core::PersonId& recipient,
                     core::Instant at) {
    return std::string{reason} + ':' + about + ':' + recipient.ToString() + ':' +
           std::to_string(at.UnixMicros());
}

/// КЛЮЧ НАПОМИНАНИЯ: повод, занятие и получатель — БЕЗ момента.
///
/// Именно отсутствие момента и делает перенос переносом. Занятие переехало на
/// другой день: обработчик кладёт строку с тем же ключом и новым сроком, и
/// напоминание переезжает вместе с занятием. С моментом в ключе строк стало бы
/// две, и человек получил бы напоминание о часе, в который его уже никто не ждёт.
std::string Reminder(std::string_view reason,
                     const core::LessonId& lesson,
                     const core::PersonId& recipient) {
    return std::string{reason} + ':' + lesson.ToString() + ':' + recipient.ToString();
}

}  // namespace

DeliverDomainEvents::DeliverDomainEvents(ports::OutboxRepository& outbox) noexcept
    : outbox_{outbox} {}

void DeliverDomainEvents::Enqueue(const core::TenantId& tenant,
                                  const core::PersonId& recipient,
                                  Channel channel,
                                  std::string_view reason,
                                  const std::string& dedup_key,
                                  core::Instant at) {
    const auto delivery =
        Delivery::Compose(tenant, recipient, channel, std::string{reason}, dedup_key, at);
    if (delivery.HasValue()) {
        outbox_.Enqueue(delivery.Value(), at);
    }
}

void DeliverDomainEvents::Remind(const core::TenantId& tenant,
                                 const core::PersonId& recipient,
                                 std::string_view reason,
                                 const core::LessonId& lesson,
                                 core::Instant starts_at,
                                 core::Instant::Duration before,
                                 core::Instant now) {
    const auto due = starts_at - before;
    const auto key = Reminder(reason, lesson, recipient);
    if (due <= now) {
        /// Срок уже прошёл — напоминать не о чем. Отзываем на случай, если
        /// строка лежит с прошлого раза: занятие могли передвинуть ближе.
        outbox_.Withdraw(tenant, key);
        return;
    }

    const auto delivery =
        Delivery::Compose(tenant, recipient, Channel::kPush, std::string{reason}, key, now);
    if (delivery.HasValue()) {
        outbox_.Enqueue(delivery.Value(), due);
    }
}

void DeliverDomainEvents::Forget(const core::TenantId& tenant,
                                 const core::PersonId& recipient,
                                 const core::LessonId& lesson) {
    outbox_.Withdraw(tenant, Reminder(ReminderDayBefore::kType, lesson, recipient));
    outbox_.Withdraw(tenant, Reminder(ReminderHourBefore::kType, lesson, recipient));
}

void DeliverDomainEvents::SubscribeTo(events::Bus& bus) {
    bus.Subscribe<GuardianshipRevoked>([this](const GuardianshipRevoked& event) {
        Enqueue(event.envelope.tenant,
                event.guardian,
                Channel::kEmail,
                GuardianshipRevoked::kType,
                Happened(GuardianshipRevoked::kType,
                         event.student.ToString(),
                         event.guardian,
                         event.envelope.occurred_at),
                event.envelope.occurred_at);
    });

    bus.Subscribe<CapabilitiesWidened>([this](const CapabilitiesWidened& event) {
        Enqueue(event.envelope.tenant,
                event.student,
                Channel::kPush,
                CapabilitiesWidened::kType,
                Happened(CapabilitiesWidened::kType,
                         event.student.ToString(),
                         event.student,
                         event.envelope.occurred_at),
                event.envelope.occurred_at);
        if (event.guardian.has_value()) {
            Enqueue(event.envelope.tenant,
                    *event.guardian,
                    Channel::kEmail,
                    CapabilitiesWidened::kType,
                    Happened(CapabilitiesWidened::kType,
                             event.student.ToString(),
                             *event.guardian,
                             event.envelope.occurred_at),
                    event.envelope.occurred_at);
        }
    });

    bus.Subscribe<WardActedAlone>([this](const WardActedAlone& event) {
        Enqueue(event.envelope.tenant,
                event.guardian,
                Channel::kEmail,
                Name(event.act),
                Happened(Name(event.act),
                         event.student.ToString(),
                         event.guardian,
                         event.envelope.occurred_at),
                event.envelope.occurred_at);
    });

    bus.Subscribe<LessonBooked>([this](const LessonBooked& event) {
        for (const auto& person : {event.student, event.tutor}) {
            Enqueue(event.envelope.tenant,
                    person,
                    Channel::kPush,
                    LessonBooked::kType,
                    Happened(LessonBooked::kType,
                             event.lesson.ToString(),
                             person,
                             event.envelope.occurred_at),
                    event.envelope.occurred_at);
            Remind(event.envelope.tenant,
                   person,
                   ReminderDayBefore::kType,
                   event.lesson,
                   event.starts_at,
                   kDayBefore,
                   event.envelope.occurred_at);
            Remind(event.envelope.tenant,
                   person,
                   ReminderHourBefore::kType,
                   event.lesson,
                   event.starts_at,
                   kHourBefore,
                   event.envelope.occurred_at);
        }
    });

    bus.Subscribe<LessonRescheduled>([this](const LessonRescheduled& event) {
        for (const auto& person : {event.student, event.tutor}) {
            Enqueue(event.envelope.tenant,
                    person,
                    Channel::kPush,
                    LessonRescheduled::kType,
                    Happened(LessonRescheduled::kType,
                             event.lesson.ToString(),
                             person,
                             event.envelope.occurred_at),
                    event.envelope.occurred_at);

            /// Напоминания переезжают вместе с занятием: ключ у них тот же,
            /// срок новый. Отдельного «отменить старое» не нужно — его и нет.
            Remind(event.envelope.tenant,
                   person,
                   ReminderDayBefore::kType,
                   event.lesson,
                   event.becomes,
                   kDayBefore,
                   event.envelope.occurred_at);
            Remind(event.envelope.tenant,
                   person,
                   ReminderHourBefore::kType,
                   event.lesson,
                   event.becomes,
                   kHourBefore,
                   event.envelope.occurred_at);
        }
    });

    bus.Subscribe<LessonCancelled>([this](const LessonCancelled& event) {
        for (const auto& person : {event.student, event.tutor}) {
            Enqueue(event.envelope.tenant,
                    person,
                    Channel::kPush,
                    LessonCancelled::kType,
                    Happened(LessonCancelled::kType,
                             event.lesson.ToString(),
                             person,
                             event.envelope.occurred_at),
                    event.envelope.occurred_at);

            /// Занятия не будет — напоминать не о чем. Письмо «напоминаем:
            /// завтра в 17:00», пришедшее после отмены, объясняет потом
            /// репетитор, а не мы.
            Forget(event.envelope.tenant, person, event.lesson);
        }
    });
}

}  // namespace pdr::notifications
