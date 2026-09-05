#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace pdr::core {

/// Значение идентификатора — шестнадцать байт (UUID). Голой строки и голого
/// int64 в домене нет: строку легко перепутать с текстом, число — с количеством.
using IdBytes = std::array<std::uint8_t, 16>;

namespace detail {

std::optional<IdBytes> ParseUuid(std::string_view text);
std::string FormatUuid(const IdBytes& bytes);
std::size_t HashBytes(const IdBytes& bytes) noexcept;

}  // namespace detail

/// Идентификатор, помеченный типом сущности. Tag — неполный тип-метка, он
/// нужен только чтобы StrongId<PersonTag> и StrongId<TenantTag> были РАЗНЫМИ
/// типами: перепутать их местами в вызове не получится, это не соглашение, а
/// ошибка компиляции.
///
/// Неявных преобразований нет ни к строке, ни к целому: чтобы получить текст,
/// надо попросить ToString(), чтобы отдать в базу — AsBytes(). Идентификатор,
/// который сам превращается в строку, через полгода оказывается в сравнении с
/// чужой строкой, и никто этого не замечает.
template<class Tag>
class StrongId final {
public:
    static std::optional<StrongId> Parse(std::string_view text) {
        const auto bytes = detail::ParseUuid(text);
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        return StrongId{*bytes};
    }

    /// Для генератора идентификаторов и адаптеров хранилища — больше ни для кого.
    static StrongId FromBytes(const IdBytes& bytes) noexcept {
        return StrongId{bytes};
    }

    const IdBytes& AsBytes() const noexcept {
        return bytes_;
    }

    std::string ToString() const {
        return detail::FormatUuid(bytes_);
    }

    friend bool operator==(const StrongId&, const StrongId&) = default;
    friend std::strong_ordering operator<=>(const StrongId&, const StrongId&) = default;

private:
    explicit StrongId(const IdBytes& bytes) noexcept : bytes_{bytes} {}

    IdBytes bytes_{};
};

template<class T>
inline constexpr bool kIsStrongId = false;

template<class Tag>
inline constexpr bool kIsStrongId<StrongId<Tag>> = true;

using TenantId = StrongId<struct TenantTag>;
using PersonId = StrongId<struct PersonTag>;
using LessonId = StrongId<struct LessonTag>;
using SeriesId = StrongId<struct SeriesTag>;
using InvoiceId = StrongId<struct InvoiceTag>;
using SkillId = StrongId<struct SkillTag>;
using MaterialId = StrongId<struct MaterialTag>;

}  // namespace pdr::core

namespace std {

template<class Tag>
struct hash<pdr::core::StrongId<Tag>> {
    std::size_t operator()(const pdr::core::StrongId<Tag>& id) const noexcept {
        return pdr::core::detail::HashBytes(id.AsBytes());
    }
};

}  // namespace std
