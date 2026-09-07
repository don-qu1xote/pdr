#include "notifications/application/deliver_domain_events.hpp"

#include <string>

#include "events/identity/capabilities_widened.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/notifications/reminder_before_return.hpp"
#include "events/notifications/reminder_day_before.hpp"
#include "events/notifications/reminder_hour_before.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"
#include "events/scheduling/time_off_applied.hpp"
#include "events/scheduling/time_off_declared.hpp"

namespace pdr::notifications {
namespace {

using CapabilitiesWidened = pdr::events::identity::CapabilitiesWidened;
using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;
using LessonCancelled = pdr::events::scheduling::LessonCancelled;
using LessonRescheduled = pdr::events::scheduling::LessonRescheduled;
using ReminderBeforeReturn = pdr::events::notifications::ReminderBeforeReturn;
using ReminderDayBefore = pdr::events::notifications::ReminderDayBefore;
using ReminderHourBefore = pdr::events::notifications::ReminderHourBefore;
using TimeOffApplied = pdr::events::scheduling::TimeOffApplied;
using TimeOffDeclared = pdr::events::scheduling::TimeOffDeclared;
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
                     const std::string& subject,
                     const core::PersonId& recipient) {
    return std::string{reason} + ':' + subject + ':' + recipient.ToString();
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
    const auto key = Reminder(reason, lesson.ToString(), recipient);
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

void DeliverDomainEvents::RemindAbout(const core::TenantId& tenant,
                                      const core::PersonId& recipient,
                                      std::string_view reason,
                                      const std::string& subject,
                                      core::Instant due,
                                      core::Instant now) {
    const auto key = Reminder(reason, subject, recipient);
    if (due <= now) {
        /// Срок уже прошёл — напоминать не о чем. Перерыв, заведённый задним
        /// числом и уже кончившийся, напоминания о выходе не заводит вовсе.
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
    outbox_.Withdraw(tenant, Reminder(ReminderDayBefore::kType, lesson.ToString(), recipient));
    outbox_.Withdraw(tenant, Reminder(ReminderHourBefore::kType, lesson.ToString(), recipient));
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
            /// ОТМЕНА ИЗ-ЗА ПЕРЕРЫВА ПИСЬМА НЕ ЗАВОДИТ. Двенадцать писем
            /// «занятие отменено», пришедших за одну секунду, — это не забота, а
            /// поломка на вид, и человек после них ищет не расписание, а кнопку
            /// «отписаться». Одно письмо на человека придёт из `TimeOffApplied`,
            /// и в нём будет сказано, что случилось со всеми занятиями сразу.
            ///
            /// Само событие при этом остаётся и уходит на каждое занятие:
            /// биллингу нужно именно поштучно, у него на каждое свои деньги.
            if (!event.time_off.has_value()) {
                Enqueue(event.envelope.tenant,
                        person,
                        Channel::kPush,
                        LessonCancelled::kType,
                        Happened(LessonCancelled::kType,
                                 event.lesson.ToString(),
                                 person,
                                 event.envelope.occurred_at),
                        event.envelope.occurred_at);
            }

            /// Занятия не будет — напоминать не о чем. Письмо «напоминаем:
            /// завтра в 17:00», пришедшее после отмены, объясняет потом
            /// репетитор, а не мы.
            ///
            /// Это делается и при отмене по перерыву, и делается поштучно:
            /// снятие напоминания — не письмо, а его отсутствие. Одно письмо на
            /// человека не отменяет двенадцати ненужных напоминаний.
            Forget(event.envelope.tenant, person, event.lesson);
        }
    });

    bus.Subscribe<TimeOffDeclared>([this](const TimeOffDeclared& event) {
        /// ВОЗВРАЩЕНИЕ. За сутки до конца перерыва — напоминание с расписанием
        /// первой недели. Две недели без занятий стирают расписание из головы, и
        /// первое утро после отпуска это «а во сколько у меня сегодня?».
        ///
        /// Строка ложится в очередь сразу и лежит до срока — как и напоминания о
        /// занятиях. Задания, которое каждый день перебирает перерывы и
        /// спрашивает «не пора ли», не нужно вовсе.
        ///
        /// ЧТО ИМЕННО ЧЕЛОВЕК ПРОЧТЁТ, СОБИРАЕТ ОТПРАВЩИК. В очереди лежит повод
        /// (`notifications.reminder_before_return`) и получатель, а слова и
        /// расписание первой недели подставляет шаблон при отправке: текст,
        /// зашитый в бэкенде, нельзя поменять без выкатки (`Delivery`).
        RemindAbout(event.envelope.tenant,
                    event.person,
                    ReminderBeforeReturn::kType,
                    event.time_off.ToString(),
                    event.to - kDayBefore,
                    event.envelope.occurred_at);
    });

    bus.Subscribe<TimeOffApplied>([this](const TimeOffApplied& event) {
        /// ОДНО ПИСЬМО НА ЧЕЛОВЕКА, А НЕ НА ЗАНЯТИЕ. Список получателей собрал
        /// издатель — он только что прошёл по всем занятиям периода, — и каждый
        /// стоит в нём один раз, сколько бы занятий ни потерял.
        ///
        /// Ключ намерения — перерыв и получатель: повторный проход обработчика
        /// второго письма не заведёт.
        for (const auto& person : event.tell) {
            Enqueue(event.envelope.tenant,
                    person,
                    Channel::kPush,
                    TimeOffApplied::kType,
                    Happened(TimeOffApplied::kType,
                             event.time_off.ToString(),
                             person,
                             event.envelope.occurred_at),
                    event.envelope.occurred_at);
        }
    });
}

}  // namespace pdr::notifications
