#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "identity/application/ports/practices.hpp"
#include "identity/core/practice.hpp"

namespace pdr::identity {

/// Попроситься в подбор — и уйти из него обратно.
///
/// ПЕРЕКЛЮЧАТЕЛЬ В НАСТРОЙКАХ, А НЕ ВОПРОС ПРИ РЕГИСТРАЦИИ. Практика заводится
/// скрытой у всех; человек, которому подбор не нужен, не узнает о нём никогда и
/// ничего не потеряет. Тот, кому он понадобился, приходит и просит — в тот
/// момент, когда вопрос стал для него осмысленным (ADR-0016).
///
/// Спрятаться обратно можно всегда и немедленно, из любого состояния и без
/// разбора: разбор нужен, чтобы стать видимым, а не чтобы перестать.
class AskToPublish final {
public:
    AskToPublish(ports::Practices& practices,
                 const application::ports::Clock& clock,
                 events::Bus& bus) noexcept;

    core::Result<Practice> Execute(const core::TenantId& tenant) const;

    /// Убрать практику из подбора. Работает и до разбора, и после него.
    core::Result<Practice> Hide(const core::TenantId& tenant) const;

private:
    ports::Practices& practices_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

/// Разобрать заявку на публикацию.
///
/// РУЧНАЯ ОЧЕРЕДЬ СТОИТ ЗДЕСЬ, А НЕ НА РЕГИСТРАЦИИ. Всё это время практика
/// работает: занятия идут, ученики приглашены, деньги считаются. Разбор решает
/// один вопрос — показывать ли эту практику незнакомым людям, — и цена ошибки в
/// нём чужая, поэтому его делает человек.
///
/// Отказ называет причину кодом из закрытого списка. Свободная строка от
/// модератора — это переписка в колонке базы: по ней потом ни отобрать, ни
/// посчитать, ни перевести, а человеку она всё равно достанется в пересказе.
///
/// Сама очередь сюда не приходит: `identity_tenant` несёт арендатора, и читать
/// её поверх практик нечем (docs/architecture/tenancy.md). Кто ждёт разбора,
/// показывает `make practice-queue` — работа оператора; сценарий решает по
/// одной названной практике.
class DecidePublication final {
public:
    DecidePublication(ports::Practices& practices,
                      const application::ports::Clock& clock,
                      events::Bus& bus) noexcept;

    core::Result<Practice> Publish(const core::TenantId& tenant) const;

    core::Result<Practice> Refuse(const core::TenantId& tenant, RefusalReason reason) const;

private:
    ports::Practices& practices_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::identity
