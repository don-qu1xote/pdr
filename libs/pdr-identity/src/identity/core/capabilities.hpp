#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

#include "core/errors.hpp"
#include "identity/core/age_status.hpp"

namespace pdr::identity {

/// Возрастные пороги самостоятельности. ТРИ, А НЕ ОДИН.
///
/// Один порог означал бы, что подросток либо ребёнок, либо взрослый, и ничего
/// между; в жизни между ними четыре года, в которые он сам двигает занятия, но
/// не распоряжается деньгами.
///
/// ЧИСЛА ЗДЕСЬ НЕ ЖИВУТ. Ни четырнадцати, ни шестнадцати, ни восемнадцати:
/// пороги приходят из динамического конфига (`PDR_SELF_ACCOUNT_AGE`,
/// `PDR_OWN_PAYMENTS_AGE`, `PDR_MAJORITY_AGE`), потому что это вопрос права и
/// страны, а не вкуса. До восемнадцати дееспособность ограничена, и сами
/// значения подлежат проверке юристом до публичного запуска
/// (docs/adr/0018-age-thresholds-are-a-legal-question.md).
enum class AgeThreshold : std::uint8_t {
    /// Двигает свои занятия и пишет отзывы.
    kSlotsAndReviews,
    /// Платит своими средствами и сам выбирает репетитора.
    kOwnPayments,
    /// Совершеннолетие: опекуна больше нет ни в одном уровне.
    kMajority,

    /// ГРАНИЦА СПИСКА, а не порог.
    kBoundary,
};

std::string_view Name(AgeThreshold threshold) noexcept;

std::optional<AgeThreshold> ParseAgeThreshold(std::string_view text);

inline constexpr std::array<AgeThreshold, 3> kEveryAgeThreshold{
    AgeThreshold::kSlotsAndReviews,
    AgeThreshold::kOwnPayments,
    AgeThreshold::kMajority,
};

static_assert(kEveryAgeThreshold.size() == static_cast<std::size_t>(AgeThreshold::kBoundary),
              "порог заведён, а в kEveryAgeThreshold его нет: обход реестра пропустит его");

/// Значения порогов, разобранные и проверенные на связность.
///
/// Схема динамического конфига задаёт пределы каждого числа по отдельности, а
/// связь между ними — нет: «платит раньше, чем двигает занятия» из неё не
/// выражается. Поэтому разбирает их домен и отвергает набор целиком; негодное
/// значение не применяется, а прежнее продолжает действовать.
class AgeThresholds final {
public:
    static core::Result<AgeThresholds> Compose(int slots_and_reviews,
                                               int own_payments,
                                               int majority);

    int Years(AgeThreshold threshold) const noexcept;

    friend bool operator==(const AgeThresholds&, const AgeThresholds&) = default;

private:
    AgeThresholds(int slots_and_reviews, int own_payments, int majority) noexcept
        : slots_and_reviews_{slots_and_reviews}, own_payments_{own_payments}, majority_{majority} {}

    int slots_and_reviews_;
    int own_payments_;
    int majority_;
};

/// Что человек может делать САМ.
///
/// Это не роли. «Взрослый ребёнок» — тот же `Role::kStudent` с другим набором
/// возможностей; отдельная роль размножила бы политики и разъехалась бы с
/// опекой, потому что роль выдают руками, а возраст приходит сам.
enum class Capability : std::uint8_t {
    /// Переносить и отменять свои занятия.
    kMoveOwnSlots,
    /// Писать отзыв о репетиторе.
    kWriteReview,
    /// Открывать и отзывать доступ своего опекуна.
    kDecideOwnGuardianAccess,
    /// Платить своими средствами.
    kPayOwnMoney,
    /// Выбирать репетитора и записываться к нему.
    kChooseTutor,

    /// ГРАНИЦА СПИСКА, а не возможность.
    kBoundary,
};

std::string_view Name(Capability capability) noexcept;

std::optional<Capability> ParseCapability(std::string_view text);

inline constexpr std::array<Capability, 5> kEveryCapability{
    Capability::kMoveOwnSlots,
    Capability::kWriteReview,
    Capability::kDecideOwnGuardianAccess,
    Capability::kPayOwnMoney,
    Capability::kChooseTutor,
};

static_assert(kEveryCapability.size() == static_cast<std::size_t>(Capability::kBoundary),
              "возможность заведена, а в kEveryCapability её нет: обход реестра пропустит её");

/// С каким порогом приходит эта возможность.
///
/// Перечисление разбирается целиком, поэтому возможность без порога не
/// собирается: забыть связать новую с возрастом нельзя.
///
/// К СОВЕРШЕННОЛЕТИЮ НОВЫХ ВОЗМОЖНОСТЕЙ НЕ ПРИБАВЛЯЕТСЯ, и это не пропуск. К
/// восемнадцати ученик уже всё перечисленное умеет сам; меняется другое — у
/// опекуна кончаются последние уровни доступа (`WhenStudentDecides`). Порог
/// совершеннолетия работает с той стороны, а не с этой.
constexpr AgeThreshold ArrivesWith(Capability capability) noexcept {
    switch (capability) {
        case Capability::kMoveOwnSlots:
        case Capability::kWriteReview:
        case Capability::kDecideOwnGuardianAccess:
        case Capability::kBoundary:
            return AgeThreshold::kSlotsAndReviews;
        case Capability::kPayOwnMoney:
        case Capability::kChooseTutor:
            return AgeThreshold::kOwnPayments;
    }
    return AgeThreshold::kSlotsAndReviews;
}

/// Набор возможностей. Отдельный тип, а не `std::set`: их пять, и держать ради
/// них дерево — способ сделать проверку прав самой дорогой операцией запроса.
class Capabilities final {
public:
    Capabilities() noexcept = default;

    static Capabilities Of(std::initializer_list<Capability> capabilities) noexcept {
        Capabilities set;
        for (const auto capability : capabilities) {
            set = set.With(capability);
        }
        return set;
    }

    static Capabilities Everything() noexcept {
        Capabilities set;
        for (const auto capability : kEveryCapability) {
            set = set.With(capability);
        }
        return set;
    }

    Capabilities With(Capability capability) const noexcept {
        Capabilities set;
        set.bits_ = static_cast<std::uint8_t>(bits_ | Bit(capability));
        return set;
    }

    bool Has(Capability capability) const noexcept {
        return (bits_ & Bit(capability)) != 0U;
    }

    bool Empty() const noexcept {
        return bits_ == 0U;
    }

    friend bool operator==(const Capabilities&, const Capabilities&) = default;

private:
    static std::uint8_t Bit(Capability capability) noexcept {
        return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(capability));
    }

    std::uint8_t bits_{0};
};

/// ЕДИНСТВЕННОЕ МЕСТО, где возраст превращается в права.
///
/// Права ВЫЧИСЛЯЮТСЯ, а не хранятся и не выдаются по заявке: колонка «может
/// платить» устарела бы в полночь дня рождения, а заявка означала бы, что
/// совершеннолетие наступает тогда, когда кто-то нажал кнопку. Чистая функция —
/// поэтому граница порога проверяется подменяемыми часами, а не ожиданием года.
///
/// Возраст неизвестен — возможностей нет ни одной: `Compute` принимает
/// `AgeStatus`, а его без даты рождения не собрать. Кто и как поступает с
/// пустой датой, решает вызывающий, и это видно в его коде.
Capabilities Compute(const AgeStatus& age, const AgeThresholds& thresholds) noexcept;

}  // namespace pdr::identity
