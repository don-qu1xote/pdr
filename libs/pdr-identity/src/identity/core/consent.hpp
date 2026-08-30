#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

using ConsentRecordId = core::StrongId<struct ConsentRecordTag>;

/// На что согласие. ДВЕ ГАЛОЧКИ, А НЕ ОДНА.
///
/// Правовой вес у них разный, и слепить их нельзя: без согласия на обработку
/// пользоваться продуктом невозможно вовсе, без согласия на запись — можно,
/// просто конспекта из записи не будет. Одна галочка на всё означала бы, что
/// человек, которому не нужна запись, отказывается от продукта целиком.
///
/// Третьего вида нет и не будет. Отдельного вопроса «а можно ли анализировать
/// ваши занятия» продукт не задаёт: основание — перечень, раскрытый прямо, а не
/// частокол переключателей, в которых нажимают «согласен» не читая
/// (docs/legal/personal-data.md).
enum class ConsentKind : std::uint8_t {
    /// Обработка по перечню. Без неё продукта нет.
    kProcessing,

    /// Запись занятий. Без неё продукт работает.
    kRecordings,

    /// ГРАНИЦА СПИСКА, а не вид согласия.
    kBoundary,
};

std::string_view Name(ConsentKind kind) noexcept;

std::optional<ConsentKind> ParseConsentKind(std::string_view text);

inline constexpr std::array<ConsentKind, 2> kEveryConsentKind{
    ConsentKind::kProcessing,
    ConsentKind::kRecordings,
};

static_assert(kEveryConsentKind.size() == static_cast<std::size_t>(ConsentKind::kBoundary),
              "вид согласия заведён, а в kEveryConsentKind его нет: перебор пропустит его");

/// Каким действием человек согласился.
///
/// Хранится, потому что «согласие получено» без ответа на «как именно» — это
/// утверждение, которое нечем подтвердить. Галочка при регистрации и
/// подтверждение новой версии в кабинете — разные действия с разной судьбой.
enum class ConsentAction : std::uint8_t {
    /// Галочка в форме заведения.
    kSignUpCheckbox,

    /// Галочка в кабинете, уже после заведения.
    kSettingsCheckbox,

    /// Подтверждение новой версии перечня.
    kVersionAccepted,

    /// ГРАНИЦА СПИСКА, а не действие.
    kBoundary,
};

std::string_view Name(ConsentAction action) noexcept;

std::optional<ConsentAction> ParseConsentAction(std::string_view text);

inline constexpr std::array<ConsentAction, 3> kEveryConsentAction{
    ConsentAction::kSignUpCheckbox,
    ConsentAction::kSettingsCheckbox,
    ConsentAction::kVersionAccepted,
};

static_assert(kEveryConsentAction.size() == static_cast<std::size_t>(ConsentAction::kBoundary),
              "действие заведено, а в kEveryConsentAction его нет");

/// Версия перечня. Число, а не дата: даты сравнивают неправильно чаще.
class PolicyVersion final {
public:
    static core::Result<PolicyVersion> Of(int number);

    int Number() const noexcept {
        return number_;
    }

    friend bool operator==(const PolicyVersion&, const PolicyVersion&) = default;
    friend auto operator<=>(const PolicyVersion&, const PolicyVersion&) = default;

private:
    explicit PolicyVersion(int number) noexcept : number_{number} {}

    int number_;
};

/// Насколько версия отличается от предыдущей.
///
/// НАЗНАЧАЕТ ЧЕЛОВЕК ПРИ ВЫПУСКЕ, А НЕ УГАДЫВАЕТ КОД. Появился новый получатель,
/// новая категория или новая цель — существенное, и согласие спрашивается
/// заново. Поправили формулировку — косметическое. Отличить «стало понятнее» от
/// «стало больше» diff'ом нельзя, и делать вид, что можно, опаснее, чем спросить.
enum class VersionChange : std::uint8_t {
    /// Спрашиваем согласие заново.
    kSubstantial,

    /// Не спрашиваем.
    kCosmetic,

    kBoundary,
};

std::string_view Name(VersionChange change) noexcept;

std::optional<VersionChange> ParseVersionChange(std::string_view text);

/// Записанное согласие: кто дал, когда, на что, какую версию и каким действием.
///
/// Всё пять — обязательные. Согласие без версии не отвечает на вопрос «на что
/// именно человек согласился», а без действия — на «чем это подтверждается»;
/// оба вопроса задают ровно тогда, когда ответить на них уже нечем.
class ConsentRecord final {
public:
    static core::Result<ConsentRecord> Give(ConsentRecordId id,
                                            core::TenantId tenant,
                                            core::PersonId subject,
                                            core::PersonId given_by,
                                            ConsentKind kind,
                                            PolicyVersion version,
                                            ConsentAction action,
                                            core::Instant given_at);

    static ConsentRecord Restore(ConsentRecordId id,
                                 core::TenantId tenant,
                                 core::PersonId subject,
                                 core::PersonId given_by,
                                 ConsentKind kind,
                                 PolicyVersion version,
                                 ConsentAction action,
                                 core::Instant given_at,
                                 std::optional<core::Instant> withdrawn_at);

    /// Отозвать. Строка остаётся: на вопрос «а было ли согласие в марте»
    /// отвечает она, а удалённая отвечает «нет», и это неправда.
    core::Result<ConsentRecord> Withdrawn(core::Instant at) const;

    const ConsentRecordId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    /// О ком согласие: ученик, в том числе ребёнок.
    const core::PersonId& Subject() const noexcept {
        return subject_;
    }
    /// Кто его дал: сам ученик или его опекун.
    const core::PersonId& GivenBy() const noexcept {
        return given_by_;
    }
    ConsentKind Kind() const noexcept {
        return kind_;
    }
    const PolicyVersion& Version() const noexcept {
        return version_;
    }
    ConsentAction Action() const noexcept {
        return action_;
    }
    core::Instant GivenAt() const noexcept {
        return given_at_;
    }
    const std::optional<core::Instant>& WithdrawnAt() const noexcept {
        return withdrawn_at_;
    }

    bool ByGuardian() const noexcept {
        return given_by_ != subject_;
    }

    bool IsLiveAt(core::Instant moment) const noexcept {
        return !withdrawn_at_.has_value() || moment < *withdrawn_at_;
    }

private:
    ConsentRecord(ConsentRecordId id,
                  core::TenantId tenant,
                  core::PersonId subject,
                  core::PersonId given_by,
                  ConsentKind kind,
                  PolicyVersion version,
                  ConsentAction action,
                  core::Instant given_at,
                  std::optional<core::Instant> withdrawn_at) noexcept;

    ConsentRecordId id_;
    core::TenantId tenant_;
    core::PersonId subject_;
    core::PersonId given_by_;
    ConsentKind kind_;
    PolicyVersion version_;
    ConsentAction action_;
    core::Instant given_at_;
    std::optional<core::Instant> withdrawn_at_;
};

/// Нужно ли спрашивать согласие заново.
///
/// `substantial_since` — самая ранняя существенная версия строго после принятой.
/// Её называет тот, кто ведёт перечень версий, а не вычисляет эта функция:
/// существенность — решение человека при выпуске.
constexpr bool NeedsReacceptance(const PolicyVersion& accepted,
                                 const PolicyVersion& current,
                                 bool substantial_between) noexcept {
    return accepted < current && substantial_between;
}

}  // namespace pdr::identity
