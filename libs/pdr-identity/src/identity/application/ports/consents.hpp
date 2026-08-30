#pragma once

#include <optional>
#include <vector>

#include "core/types/ids.hpp"
#include "identity/core/consent.hpp"

namespace pdr::identity::ports {

/// Записанные согласия. Узкий порт: сохранить и посмотреть, что есть у человека.
class Consents {
public:
    Consents(const Consents&) = delete;
    Consents& operator=(const Consents&) = delete;

    virtual ~Consents() = default;

    /// Все согласия о человеке, включая отозванные: строка остаётся, и на
    /// вопрос «а было ли согласие в марте» отвечает она.
    virtual std::vector<ConsentRecord> Of(const core::TenantId& tenant,
                                          const core::PersonId& subject) const = 0;

    virtual void Save(const ConsentRecord& record) = 0;

protected:
    Consents() = default;
};

/// Какая версия перечня действует сейчас и была ли она существенной.
///
/// Существенность назначает человек при выпуске версии, а не вычисляет код:
/// отличить «стало понятнее» от «стало больше» diff'ом нельзя. Порт отдаёт то,
/// что назвали, — и `SubstantialAfter` отвечает на единственный вопрос,
/// который нужен: «было ли между принятой версией и текущей хоть одно
/// существенное изменение».
class PolicyVersions {
public:
    PolicyVersions(const PolicyVersions&) = delete;
    PolicyVersions& operator=(const PolicyVersions&) = delete;

    virtual ~PolicyVersions() = default;

    virtual PolicyVersion Current() const = 0;

    virtual bool SubstantialAfter(const PolicyVersion& accepted) const = 0;

protected:
    PolicyVersions() = default;
};

}  // namespace pdr::identity::ports
