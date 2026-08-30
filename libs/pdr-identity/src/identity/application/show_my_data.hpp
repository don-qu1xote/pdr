#pragma once

#include <optional>
#include <span>
#include <vector>

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "identity/application/ports/consents.hpp"
#include "identity/core/consent.hpp"
#include "identity/core/personal_data.hpp"

namespace pdr::identity {

/// Согласие в том виде, в каком его показывают человеку.
struct ConsentShown final {
    ConsentKind kind;
    PolicyVersion version;
    ConsentAction action;
    core::Instant given_at;
    bool by_guardian{false};
    bool live{false};

    friend bool operator==(const ConsentShown&, const ConsentShown&) = default;
};

/// Содержимое экрана «мои данные».
///
/// ПЕРЕЧЕНЬ БЕЗ ТАКОГО ЭКРАНА — ОБЕЩАНИЕ БЕЗ ИСПОЛНЕНИЯ. Экран показывает
/// ВСЕ категории перечня и ВСЕХ получателей, а не те, до которых дошли руки:
/// умолчавший о категории экран — это данные, о которых человеку не сказали.
///
/// Категории приходят полным списком (`kEveryPersonalDataCategory`), а не
/// выборкой «что у нас уже есть»: половины этих данных в дереве ещё нет, и
/// человеку об этом сообщает текст перечня, а не пустое место на экране.
struct MyData final {
    std::vector<PersonalDataCategory> categories;
    std::vector<Recipient> recipients;
    std::vector<ConsentShown> consents;

    /// Нужно ли подтвердить новую версию перечня.
    bool asks_to_accept_again{false};

    /// Версия, действующая сейчас.
    PolicyVersion current_version;
};

/// Собрать экран «мои данные».
class ShowMyData final {
public:
    ShowMyData(const ports::Consents& consents,
               const ports::PolicyVersions& versions,
               const application::ports::Clock& clock) noexcept;

    core::Result<MyData> Execute(const core::TenantId& tenant, const core::PersonId& subject) const;

private:
    const ports::Consents& consents_;
    const ports::PolicyVersions& versions_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
