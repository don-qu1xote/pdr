#pragma once

#include <optional>
#include <span>

#include "core/errors.hpp"
#include "core/types/time.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/capabilities.hpp"
#include "identity/core/guardian_consent.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity {

/// С какого порога этот уровень требует слова САМОГО ученика.
///
/// Конспекты и записи — про содержание учёбы и про голос человека: там слово
/// ученика нужно уже с первого порога, и приватность подростка начинается
/// раньше его самостоятельности в деньгах.
///
/// Расписание и деньги держатся до совершеннолетия: за занятия чаще всего
/// платит родитель, и обрывать это в день четырнадцатилетия значило бы ломать
/// оплату посреди учебного года. Но и они не навсегда — с совершеннолетием
/// опекуна не остаётся ни в одном уровне, иначе «взрослый» не означало бы
/// ничего.
constexpr AgeThreshold WhenStudentDecides(GuardianScope scope) noexcept {
    switch (scope) {
        case GuardianScope::kNotesAndHomework:
        case GuardianScope::kRecordings:
            return AgeThreshold::kSlotsAndReviews;
        case GuardianScope::kSchedule:
        case GuardianScope::kPayments:
        case GuardianScope::kBoundary:
            return AgeThreshold::kMajority;
    }
    return AgeThreshold::kMajority;
}

/// Правило совершеннолетия: с каких возрастов ученик решает сам и сколько
/// времени даётся на это решение.
///
/// ОКНО, А НЕ ОБРЫВ. В день рождения доступ родителя не гаснет: уведомляются
/// обе стороны, и у них есть срок договориться. Мгновенное отключение ломает
/// семьи в середине учебного года — родитель перестаёт видеть занятия, за
/// которые платит, и виноватой оказывается платформа.
///
/// Числа приходят из динамического конфига (`PDR_SELF_ACCOUNT_AGE`,
/// `PDR_OWN_PAYMENTS_AGE`, `PDR_MAJORITY_AGE`, `PDR_GUARDIAN_HANDOVER_DAYS`), а
/// не из этого файла: возрастные пороги — вопрос права и страны, а длина окна —
/// вопрос того, как быстро люди читают почту.
class MaturityRule final {
public:
    static core::Result<MaturityRule> Compose(AgeThresholds thresholds,
                                              core::Instant::Duration grace);

    const AgeThresholds& Thresholds() const noexcept {
        return thresholds_;
    }

    /// Возраст, с которого этот уровень переходит к ученику.
    int ThresholdYears(GuardianScope scope) const noexcept {
        return thresholds_.Years(WhenStudentDecides(scope));
    }

    core::Instant::Duration Grace() const noexcept {
        return grace_;
    }

    friend bool operator==(const MaturityRule&, const MaturityRule&) = default;

private:
    MaturityRule(AgeThresholds thresholds, core::Instant::Duration grace) noexcept
        : thresholds_{thresholds}, grace_{grace} {}

    AgeThresholds thresholds_;
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
/// `guardianship_holds` — действует ли между этими двумя опека прямо сейчас.
///
/// Согласие, выданное НА ОСНОВАНИИ опеки, держится ею: отозвали опеку — доступ
/// кончился, сколько бы строк согласий ни осталось. Согласие, которое взрослый
/// выдал сам, опеки не требует вовсе — у него её и нет.
GuardianAccess WeighConsents(std::span<const GuardianConsent> consents,
                             const std::optional<BirthDate>& student_born_on,
                             const MaturityRule& rule,
                             core::Instant now,
                             bool guardianship_holds);

}  // namespace pdr::identity
