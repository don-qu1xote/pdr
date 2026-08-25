#pragma once

#include "core/types/ids.hpp"

namespace pdr::identity {

/// Публичный контракт контекста identity — ЕДИНСТВЕННЫЙ его заголовок, который
/// другим контекстам разрешено включать. Всё остальное в модуле для них не
/// существует: каталог src в чужую сборку не попадает, а попытку включить
/// внутренность ловит scripts/check_layers.py.
///
/// Контракт говорит на платформенных типах (`core::`) и простых значениях.
/// Доменные value-объекты контекста границу не пересекают: чужой контекст не
/// обязан знать правила их разбора, а мы не обязаны хранить их форму навсегда.
class Contract {
public:
    Contract(const Contract&) = delete;
    Contract& operator=(const Contract&) = delete;

    virtual ~Contract() = default;

    /// Вправе ли `actor` действовать от имени `student`.
    ///
    /// Истина в двух случаях: он и есть этот ученик (самостоятельный взрослый —
    /// языки, подготовка в вуз: опекуна у него нет вовсе) либо между ними есть
    /// действующая опека.
    virtual bool MayActFor(const core::TenantId& tenant,
                           const core::PersonId& actor,
                           const core::PersonId& student) const = 0;

protected:
    Contract() = default;
};

}  // namespace pdr::identity
