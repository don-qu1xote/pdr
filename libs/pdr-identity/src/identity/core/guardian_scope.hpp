#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace pdr::identity {

/// Уровни доступа опекуна. КАЖДЫЙ ВКЛЮЧАЕТСЯ ОТДЕЛЬНО.
///
/// Единого «родитель видит всё» здесь нет и не будет. Родитель, которому нужно
/// платить за занятия, и родитель, которому нужно слушать записи уроков, — это
/// два разных согласия, а не одно; сложенные в один флаг, они превращаются в
/// «включи, чтобы работало», и включают всё.
///
/// Порядок значений — по возрастанию чувствительности, и он содержательный:
/// расписание видно и так, а запись занятия — это голос ребёнка.
enum class GuardianScope : std::uint8_t {
    kSchedule,
    kPayments,
    kNotesAndHomework,
    kRecordings,

    /// ГРАНИЦА СПИСКА, а не уровень: `kEveryGuardianScope` обязан содержать
    /// ровно столько же значений, и это сверяет static_assert.
    kBoundary,
};

std::string_view Name(GuardianScope scope) noexcept;

std::optional<GuardianScope> ParseGuardianScope(std::string_view text);

inline constexpr std::array<GuardianScope, 4> kEveryGuardianScope{
    GuardianScope::kSchedule,
    GuardianScope::kPayments,
    GuardianScope::kNotesAndHomework,
    GuardianScope::kRecordings,
};

static_assert(kEveryGuardianScope.size() == static_cast<std::size_t>(GuardianScope::kBoundary),
              "уровень заведён, а в kEveryGuardianScope его нет: обход реестра пропустит его");

/// ЗАПИСИ ЗАНЯТИЙ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНЫ — и это решение, а не забывчивость.
///
/// Даже родителю несовершеннолетнего. Запись урока — это час голоса ребёнка,
/// его ошибок и его разговора с чужим взрослым; включать такое молча, «раз уж
/// он родитель», нельзя. Остальные три уровня открываются вместе с опекой:
/// без расписания, денег и домашних заданий опекун не может делать то, ради
/// чего опека и заводится.
constexpr bool OpensWithGuardianship(GuardianScope scope) noexcept {
    return scope != GuardianScope::kRecordings;
}

/// Набор уровней. Отдельный тип, а не `std::set`: значений четыре, и хранить
/// ради них дерево — способ сделать проверку прав самой дорогой операцией
/// запроса.
class GuardianScopeSet final {
public:
    GuardianScopeSet() noexcept = default;

    static GuardianScopeSet Of(std::initializer_list<GuardianScope> scopes) noexcept {
        GuardianScopeSet set;
        for (const auto scope : scopes) {
            set = set.With(scope);
        }
        return set;
    }

    /// То, что открывается вместе с опекой. Записей занятий здесь нет.
    static GuardianScopeSet OpenedByGuardianship() noexcept {
        GuardianScopeSet set;
        for (const auto scope : kEveryGuardianScope) {
            if (OpensWithGuardianship(scope)) {
                set = set.With(scope);
            }
        }
        return set;
    }

    static GuardianScopeSet Everything() noexcept {
        GuardianScopeSet set;
        for (const auto scope : kEveryGuardianScope) {
            set = set.With(scope);
        }
        return set;
    }

    GuardianScopeSet With(GuardianScope scope) const noexcept {
        GuardianScopeSet set;
        set.bits_ = static_cast<std::uint8_t>(bits_ | Bit(scope));
        return set;
    }

    GuardianScopeSet Without(GuardianScope scope) const noexcept {
        GuardianScopeSet set;
        set.bits_ = static_cast<std::uint8_t>(bits_ & ~Bit(scope));
        return set;
    }

    bool Has(GuardianScope scope) const noexcept {
        return (bits_ & Bit(scope)) != 0U;
    }

    bool Empty() const noexcept {
        return bits_ == 0U;
    }

    friend bool operator==(const GuardianScopeSet&, const GuardianScopeSet&) = default;

private:
    static std::uint8_t Bit(GuardianScope scope) noexcept {
        return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(scope));
    }

    std::uint8_t bits_{0};
};

}  // namespace pdr::identity
