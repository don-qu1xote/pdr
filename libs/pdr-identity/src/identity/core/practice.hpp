#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

/// Видна ли практика снаружи.
///
/// ВЫКЛЮЧЕНА У ВСЕХ ПО УМОЛЧАНИЮ, и это не осторожность, а форма продукта.
/// Репетитор, переносящий двадцать своих учеников, пришёл не за подбором: он
/// пришёл работать. Развилки «зачем вы пришли» при регистрации не существует
/// (ADR-0016), поэтому и вопроса «показывать ли вас в поиске» при заведении
/// практики нет — ответ на него один для всех, и он «нет».
///
/// РЕГИСТРАЦИЯ СВОБОДНА, МОДЕРИРУЕТСЯ ВИДИМОСТЬ. Разбор руками стоит на
/// публикации, а не на входе: очередь на входе означает, что человек с двадцатью
/// учениками ждёт, пока его посмотрят, — и уходит.
enum class Visibility : std::uint8_t {
    /// Практика работает, снаружи её нет. Так у всех с первого дня.
    kHidden,
    /// Владелец попросил публикацию, разбора ещё не было.
    kPending,
    /// Разобрали и опубликовали: практика попадает в подбор.
    kPublished,
    /// Разобрали и отказали. Попросить снова можно.
    kRefused,

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(Visibility visibility) noexcept;

std::optional<Visibility> ParseVisibility(std::string_view text);

inline constexpr std::array<Visibility, 4> kEveryVisibility{
    Visibility::kHidden,
    Visibility::kPending,
    Visibility::kPublished,
    Visibility::kRefused,
};

static_assert(kEveryVisibility.size() == static_cast<std::size_t>(Visibility::kBoundary),
              "состояние заведено, а в kEveryVisibility его нет: обход реестра пропустит его");

/// Почему отказали в публикации. КОД, А НЕ ТЕКСТ: свободная строка в отказе
/// модератора — это переписка с человеком в колонке базы, и через полгода по
/// ней нельзя ни отобрать, ни посчитать, ни перевести.
enum class RefusalReason : std::uint8_t {
    /// В профиле нечего показывать: ни предметов, ни описания.
    kNothingToShow,
    /// Похоже на чужое имя или чужие документы.
    kLooksBorrowed,
    /// Зовёт работать мимо площадки прямо в описании.
    kCallsAway,

    /// ГРАНИЦА СПИСКА, а не причина.
    kBoundary,
};

std::string_view Name(RefusalReason reason) noexcept;

std::optional<RefusalReason> ParseRefusalReason(std::string_view text);

inline constexpr std::array<RefusalReason, 3> kEveryRefusalReason{
    RefusalReason::kNothingToShow,
    RefusalReason::kLooksBorrowed,
    RefusalReason::kCallsAway,
};

static_assert(kEveryRefusalReason.size() == static_cast<std::size_t>(RefusalReason::kBoundary),
              "причина заведена, а в kEveryRefusalReason её нет");

/// Практика: арендатор со стороны «видно ли его снаружи».
///
/// Имя и часовой пояс живут в `Tenant`; здесь только состояние видимости и его
/// переходы. Разделено затем, что имя правят каждый месяц, а видимость — один
/// раз за всё время, и складывать их в одно значение значит переписывать
/// строку модерации при каждой правке названия.
class Practice final {
public:
    /// Заведена. Скрыта — как и все.
    static Practice Opened(core::TenantId tenant, core::Instant at) noexcept;

    static Practice Restore(core::TenantId tenant,
                            Visibility visibility,
                            core::Instant opened_at,
                            std::optional<core::Instant> asked_at,
                            std::optional<core::Instant> decided_at,
                            std::optional<RefusalReason> refusal) noexcept;

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    Visibility Visible() const noexcept {
        return visibility_;
    }
    core::Instant OpenedAt() const noexcept {
        return opened_at_;
    }
    const std::optional<core::Instant>& AskedAt() const noexcept {
        return asked_at_;
    }
    const std::optional<core::Instant>& DecidedAt() const noexcept {
        return decided_at_;
    }
    const std::optional<RefusalReason>& Refusal() const noexcept {
        return refusal_;
    }

    /// Попадает ли практика в подбор. ЕДИНСТВЕННЫЙ вопрос, который задают
    /// снаружи, и ответ на него по умолчанию «нет».
    bool IsDiscoverable() const noexcept {
        return visibility_ == Visibility::kPublished;
    }

    /// Владелец просит опубликовать. Из скрытой или из отказанной: отказ не
    /// приговор, человек дописывает профиль и просит снова.
    core::Result<Practice> AskedToPublish(core::Instant at) const;

    core::Result<Practice> Published(core::Instant at) const;

    core::Result<Practice> RefusedBecause(RefusalReason reason, core::Instant at) const;

    /// Владелец убирает практику из подбора. Отказа здесь не бывает ни из
    /// какого состояния: спрятаться человек вправе всегда и немедленно.
    Practice Hidden(core::Instant at) const noexcept;

    friend bool operator==(const Practice&, const Practice&) = default;

private:
    Practice(core::TenantId tenant,
             Visibility visibility,
             core::Instant opened_at,
             std::optional<core::Instant> asked_at,
             std::optional<core::Instant> decided_at,
             std::optional<RefusalReason> refusal) noexcept
        : tenant_{std::move(tenant)},
          visibility_{visibility},
          opened_at_{opened_at},
          asked_at_{asked_at},
          decided_at_{decided_at},
          refusal_{refusal} {}

    core::TenantId tenant_;
    Visibility visibility_;
    core::Instant opened_at_;
    std::optional<core::Instant> asked_at_;
    std::optional<core::Instant> decided_at_;
    std::optional<RefusalReason> refusal_;
};

}  // namespace pdr::identity
