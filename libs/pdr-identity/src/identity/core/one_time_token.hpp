#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/digest.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity {

/// Идентификатор строки одноразового токена. Своя метка: перепутать его с
/// человеком или с сессией не должно получаться.
using TokenId = core::StrongId<struct TokenTag>;

/// Зачем выдан токен. Список закрыт и повторяет `identity_one_time_token_purpose_known`.
enum class TokenPurpose : std::uint8_t {
    kInvitation,
    kPasswordReset,
};

std::string_view Name(TokenPurpose purpose) noexcept;

std::optional<TokenPurpose> ParseTokenPurpose(std::string_view text);

/// Секрет из одноразовой ссылки — то, что человек получает письмом.
///
/// В базе его нет: там лежит только отпечаток (`Digest`). Утёкшая копия базы не
/// даёт войти по чужой ссылке, потому что из отпечатка секрет не восстановить —
/// в этом весь смысл, и поэтому типы разные.
class TokenSecret final {
public:
    /// Не меньше 32 байт случайности, то есть 43 знаков base64url. Короткий
    /// токен подбирается перебором ссылок, и заметить это некому.
    static constexpr std::size_t kLeastLength = 43;

    static core::Result<TokenSecret> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

private:
    explicit TokenSecret(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

/// Одноразовый токен: приглашение ученика или сброс пароля.
///
/// ОДИН МЕХАНИЗМ НА ДВА СЛУЧАЯ, и это решение, а не экономия. У приглашения и
/// у сброса ровно одни и те же правила: секрет живёт коротко, срабатывает один
/// раз и хранится отпечатком. Два похожих механизма означали бы, что через
/// полгода срок жизни поправят у одного.
///
/// Различие между случаями — в том, на что токен указывает: приглашение несёт
/// РОЛЬ (человека, которого позвали, ещё нет), сброс несёт ЧЕЛОВЕКА (он давно
/// есть). Одновременно оба не бывают, и это же ограничение стоит в схеме.
class OneTimeToken final {
public:
    static core::Result<OneTimeToken> Invitation(TokenId id,
                                                 core::TenantId tenant,
                                                 Digest secret,
                                                 Role role,
                                                 std::optional<Digest> invited,
                                                 core::Instant at,
                                                 core::Instant::Duration lifetime);

    static core::Result<OneTimeToken> PasswordReset(TokenId id,
                                                    core::TenantId tenant,
                                                    Digest secret,
                                                    core::PersonId person,
                                                    core::Instant at,
                                                    core::Instant::Duration lifetime);

    /// Собрать из хранилища: там строка уже прошла ограничения схемы.
    static OneTimeToken Restore(TokenId id,
                                core::TenantId tenant,
                                Digest secret,
                                TokenPurpose purpose,
                                std::optional<Role> role,
                                std::optional<core::PersonId> person,
                                std::optional<Digest> invited,
                                core::Instant created_at,
                                core::Instant expires_at,
                                std::optional<core::Instant> used_at);

    const TokenId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const Digest& Secret() const noexcept {
        return secret_;
    }
    TokenPurpose Purpose() const noexcept {
        return purpose_;
    }
    /// Кем позвали. Есть только у приглашения.
    const std::optional<Role>& InvitedAs() const noexcept {
        return role_;
    }
    /// Кому меняют пароль. Есть только у сброса.
    const std::optional<core::PersonId>& Person() const noexcept {
        return person_;
    }
    /// Отпечаток адреса, на который выдано приглашение.
    ///
    /// Отпечаток, а не адрес: строка приглашения живёт неделю и всё это время
    /// хранит, кого позвали. Отпечатка хватает, чтобы не послать второго
    /// письма тому же человеку, и не хватает, чтобы прочитать список учеников
    /// из дампа.
    ///
    /// Пусто у приглашений, выданных ссылкой «позвать кого угодно», и у
    /// сбросов пароля.
    const std::optional<Digest>& Invited() const noexcept {
        return invited_;
    }
    core::Instant CreatedAt() const noexcept {
        return created_at_;
    }
    core::Instant ExpiresAt() const noexcept {
        return expires_at_;
    }
    const std::optional<core::Instant>& UsedAt() const noexcept {
        return used_at_;
    }

    bool IsUsableAt(core::Instant moment) const noexcept;

    /// Сработать. Второй раз — отказ, а не молчаливое согласие: пересланная
    /// ссылка не должна пускать второго.
    core::Result<OneTimeToken> Used(core::Instant at) const;

    friend bool operator==(const OneTimeToken&, const OneTimeToken&) = default;

private:
    OneTimeToken(TokenId id,
                 core::TenantId tenant,
                 Digest secret,
                 TokenPurpose purpose,
                 std::optional<Role> role,
                 std::optional<core::PersonId> person,
                 std::optional<Digest> invited,
                 core::Instant created_at,
                 core::Instant expires_at,
                 std::optional<core::Instant> used_at) noexcept
        : id_{std::move(id)},
          tenant_{std::move(tenant)},
          secret_{std::move(secret)},
          purpose_{purpose},
          role_{role},
          person_{std::move(person)},
          invited_{std::move(invited)},
          created_at_{created_at},
          expires_at_{expires_at},
          used_at_{used_at} {}

    TokenId id_;
    core::TenantId tenant_;
    Digest secret_;
    TokenPurpose purpose_;
    std::optional<Role> role_;
    std::optional<core::PersonId> person_;
    std::optional<Digest> invited_;
    core::Instant created_at_;
    core::Instant expires_at_;
    std::optional<core::Instant> used_at_;
};

}  // namespace pdr::identity
