#pragma once

#include <optional>
#include <span>

#include "core/errors.hpp"
#include "core/types/time.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/guardian_consent.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity {

/// Правило совершеннолетия: с какого возраста ученик решает сам и сколько
/// времени даётся на это решение.
///
/// ОКНО, А НЕ ОБРЫВ. В день рождения доступ родителя не гаснет: уведомляются
/// обе стороны, и у них есть срок договориться. Мгновенное отключение ломает
/// семьи в середине учебного года — родитель перестаёт видеть занятия, за
/// которые платит, и виноватой оказывается платформа.
///
/// Оба числа приходят из динамического конфига (`PDR_SELF_ACCOUNT_AGE`,
/// `PDR_GUARDIAN_HANDOVER`), а не из этого файла: возрастной порог — вопрос
/// права и страны, а длина окна — вопрос того, как быстро люди читают почту.
class MaturityRule final {
public:
    static core::Result<MaturityRule> Compose(int threshold_years, core::Instant::Duration grace);

    int ThresholdYears() const noexcept {
        return threshold_years_;
    }
    core::Instant::Duration Grace() const noexcept {
        return grace_;
    }

    friend bool operator==(const MaturityRule&, const MaturityRule&) = default;

private:
    MaturityRule(int threshold_years, core::Instant::Duration grace) noexcept
        : threshold_years_{threshold_years}, grace_{grace} {}

    int threshold_years_;
    core::Instant::Duration grace_;
};

/// Что опекуну открыто в этот момент.
///
/// Три набора, и третий — не то же самое, что «не открыто». Человеку нужно
/// сказать разное: «этот уровень вам не включали» и «ученик вырос, и теперь
/// слово за ним» — разные ответы, после которых он идёт к разным людям.
class GuardianAccess final {
public:
    GuardianAccess(GuardianScopeSet open,
                   GuardianScopeSet deciding,
                   GuardianScopeSet awaits_student) noexcept
        : open_{open}, deciding_{deciding}, awaits_student_{awaits_student} {}

    /// Открыто прямо сейчас.
    const GuardianScopeSet& Open() const noexcept {
        return open_;
    }

    /// Ещё открыто, но окно на решение уже идёт: ученик стал взрослым.
    /// Подмножество открытого — это повод уведомить, а не отказать.
    const GuardianScopeSet& Deciding() const noexcept {
        return deciding_;
    }

    /// Закрыто до слова ученика: он вырос, окно вышло, согласия он не дал.
    const GuardianScopeSet& AwaitsStudent() const noexcept {
        return awaits_student_;
    }

    friend bool operator==(const GuardianAccess&, const GuardianAccess&) = default;

private:
    GuardianScopeSet open_;
    GuardianScopeSet deciding_;
    GuardianScopeSet awaits_student_;
};

/// ЕДИНСТВЕННОЕ МЕСТО, где из согласий и возраста получается доступ.
///
/// Чистая функция: ни хранилища, ни часов — момент приходит параметром. Поэтому
/// переход через совершеннолетие проверяется подменяемыми часами за
/// миллисекунды, а не ожиданием дня рождения.
///
/// Дата рождения может быть неизвестна — у людей, заведённых до того, как её
/// стали спрашивать. Тогда правило совершеннолетия НЕ СРАБАТЫВАЕТ: отобрать
/// доступ у родителя из-за пустой колонки хуже, чем оставить его до выяснения.
/// Что с такими делать — docs/runbooks/guardian-access.md.
GuardianAccess WeighConsents(std::span<const GuardianConsent> consents,
                             const std::optional<BirthDate>& student_born_on,
                             const MaturityRule& rule,
                             core::Instant now);

}  // namespace pdr::identity
