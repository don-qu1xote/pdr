#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/login_attempts.hpp"
#include "identity/application/ports/one_time_tokens.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/session_store.hpp"

namespace pdr::identity::testing {

/// Хранилища входа в памяти: то, на чём проверяются сценарии.
///
/// Настоящих Argon2 и SHA-256 здесь нет — им нужен userver, а unit-тесту не
/// нужен ни он, ни база, ни докер. Фейки повторяют не устройство адаптеров, а
/// их ОБЕЩАНИЯ: отпечаток одинаков для одинакового текста, счётчик попыток
/// прибавляет и читает одной операцией, хранилище сессий не выдаёт чужого
/// арендатора.

/// Отпечаток без криптографии: годится ровно на то, чтобы одинаковый текст
/// давал одинаковые 64 знака, а разный — разные.
class FakeDigests final : public ports::Digests {
public:
    Digest Of(std::string_view text) const override {
        std::ostringstream out;
        out << std::hex;
        const auto value = std::hash<std::string_view>{}(text);
        for (int part = 0; part < 4; ++part) {
            out.width(16);
            out.fill('0');
            out << (value + static_cast<std::size_t>(part));
        }

        auto hex = out.str();
        hex.resize(64, '0');
        return Digest::Parse(hex).Value();
    }
};

/// «Хеш», который не считает ничего: запись Argon2id только по форме. Проверить
/// сценарий это позволяет, а подобрать по нему пароль — тем более.
class FakeHasher final : public ports::PasswordHasher {
public:
    core::Result<PasswordHash> Hash(const Password& password,
                                    const PasswordRules& rules) const override {
        ++counted_;
        return PasswordHash::Parse("$argon2id$fake$m=" + std::to_string(rules.MemoryKib()) + "$" +
                                   password.Secret());
    }

    bool Matches(const Password& password, const PasswordHash& hash) const override {
        ++counted_;
        return hash.Value().size() > password.Secret().size() &&
               hash.Value().compare(hash.Value().size() - password.Secret().size(),
                                    password.Secret().size(),
                                    password.Secret()) == 0;
    }

    /// Сколько раз считали. Нужно ровно одному тесту: неизвестная почта обязана
    /// стоить столько же, сколько известная.
    std::size_t Counted() const noexcept {
        return counted_;
    }

private:
    mutable std::size_t counted_{0};
};

/// Пароли и люди лежат в разных таблицах, и фейк повторяет именно это: почту с
/// человеком связывает `identity_person` (её заполняет заведение участника), а
/// хеш — `identity_credential`. Слепи их фейк в одну карту — и тест перестал бы
/// замечать, что человека завели, а пароль ему не задали.
class FakeCredentials final : public ports::CredentialStore {
public:
    /// Человек появился: почта теперь указывает на него. Пароля ещё нет.
    void Register(const core::TenantId& tenant, const core::PersonId& person, const Email& mail) {
        mails_.insert_or_assign(Key(tenant, mail.Value()), person);
    }

    void Put(const core::TenantId& tenant,
             const core::PersonId& person,
             const Email& mail,
             const PasswordHash& hash) {
        Register(tenant, person, mail);
        hashes_.insert_or_assign(Key(tenant, person.ToString()), hash);
    }

    std::optional<ports::Credential> FindByEmail(const core::TenantId& tenant,
                                                 const Email& mail) const override {
        const auto person = mails_.find(Key(tenant, mail.Value()));
        if (person == mails_.end()) {
            return std::nullopt;
        }
        const auto hash = hashes_.find(Key(tenant, person->second.ToString()));
        if (hash == hashes_.end()) {
            return std::nullopt;
        }
        return ports::Credential{person->second, hash->second};
    }

    std::optional<PasswordHash> FindFor(const core::TenantId& tenant,
                                        const core::PersonId& person) const override {
        const auto hash = hashes_.find(Key(tenant, person.ToString()));
        if (hash == hashes_.end()) {
            return std::nullopt;
        }
        return hash->second;
    }

    void Save(const core::TenantId& tenant,
              const core::PersonId& person,
              const PasswordHash& hash) override {
        hashes_.insert_or_assign(Key(tenant, person.ToString()), hash);
    }

private:
    static std::string Key(const core::TenantId& tenant, const std::string& what) {
        return tenant.ToString() + "|" + what;
    }

    std::unordered_map<std::string, core::PersonId> mails_;
    std::unordered_map<std::string, PasswordHash> hashes_;
};

class FakeSessions final : public ports::SessionStore {
public:
    void Save(const Session& session) override {
        const auto key = session.Id().ToString();
        const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Session& stored) {
            return stored.Id().ToString() == key;
        });
        if (found == rows_.end()) {
            rows_.push_back(session);
            return;
        }
        *found = session;
    }

    std::optional<Session> Find(const SessionId& id) const override {
        const auto key = id.ToString();
        const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Session& stored) {
            return stored.Id().ToString() == key;
        });
        if (found == rows_.end()) {
            return std::nullopt;
        }
        return *found;
    }

    void RevokeAllFor(const core::TenantId& tenant,
                      const core::PersonId& person,
                      core::Instant at) override {
        for (auto& stored : rows_) {
            if (stored.Tenant() == tenant && stored.Person() == person) {
                stored = stored.Revoked(at);
            }
        }
    }

    std::size_t Count() const noexcept {
        return rows_.size();
    }

private:
    std::vector<Session> rows_;
};

class FakeTokens final : public ports::OneTimeTokens {
public:
    void Issue(const OneTimeToken& token) override {
        rows_.push_back(token);
    }

    std::optional<OneTimeToken> Find(const core::TenantId& tenant,
                                     const Digest& secret) const override {
        const auto found =
            std::find_if(rows_.begin(), rows_.end(), [&](const OneTimeToken& stored) {
                return stored.Tenant() == tenant && stored.Secret() == secret;
            });
        if (found == rows_.end()) {
            return std::nullopt;
        }
        return *found;
    }

    void MarkUsed(const OneTimeToken& token) override {
        const auto found =
            std::find_if(rows_.begin(), rows_.end(), [&](const OneTimeToken& stored) {
                return stored.Id() == token.Id();
            });
        if (found != rows_.end()) {
            *found = token;
        }
    }

private:
    std::vector<OneTimeToken> rows_;
};

class FakeAttempts final : public ports::LoginAttempts {
public:
    AttemptWindow Register(const core::TenantId& tenant,
                           AttemptSubject subject,
                           const Digest& of,
                           core::Instant at,
                           core::Instant::Duration window) override {
        const auto key = Key(tenant, subject, of);
        const auto found = rows_.find(key);
        const auto next = found == rows_.end() ? AttemptWindow::Restore(at, 1)
                                               : found->second.Registered(at, window);
        rows_.insert_or_assign(key, next);
        return next;
    }

    AttemptWindow Seen(const core::TenantId& tenant,
                       AttemptSubject subject,
                       const Digest& of) const override {
        const auto found = rows_.find(Key(tenant, subject, of));
        if (found == rows_.end()) {
            return AttemptWindow::Restore(core::Instant::FromUnixMicros(0), 0);
        }
        return found->second;
    }

    void Forget(const core::TenantId& tenant, AttemptSubject subject, const Digest& of) override {
        rows_.erase(Key(tenant, subject, of));
    }

private:
    static std::string Key(const core::TenantId& tenant, AttemptSubject subject, const Digest& of) {
        return tenant.ToString() + "|" + std::string{Name(subject)} + "|" + of.Value();
    }

    std::unordered_map<std::string, AttemptWindow> rows_;
};

/// Заводит человека ровно так же, как адаптер: строка человека и есть связь
/// почты с ним, а пароль ложится отдельно и позже.
class FakeDirectory final : public ports::ParticipantDirectory {
public:
    explicit FakeDirectory(FakeCredentials& people) noexcept : people_{people} {}

    core::Result<void> Enrol(const core::TenantId& tenant,
                             const ports::Enrolment& enrolment) override {
        const auto key = tenant.ToString() + "|" + enrolment.person.Mail().Value();
        if (!taken_.insert(key).second) {
            return core::Error{core::ErrorKind::kConflict,
                               "participant_email_taken",
                               "эта почта в кабинете уже занята"};
        }

        people_.Register(tenant, enrolment.person.Id(), enrolment.person.Mail());
        enrolled_.push_back(enrolment);
        return {};
    }

    const std::vector<ports::Enrolment>& Enrolled() const noexcept {
        return enrolled_;
    }

private:
    FakeCredentials& people_;
    std::set<std::string> taken_;
    std::vector<ports::Enrolment> enrolled_;
};

/// Настройки, которые можно двигать прямо в тесте.
class FakeSettings final : public ports::AuthSettings {
public:
    FakeSettings()
        : passwords_{PasswordRules::Compose(19456, 2, 1, 10).Value()},
          throttle_{ThrottleLimits::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                                std::chrono::minutes{15}),
                                            3,
                                            10)
                        .Value()},
          lifetimes_{
              AuthLifetimes::Compose(
                  std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24}),
                  std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{48}),
                  std::chrono::duration_cast<core::Instant::Duration>(std::chrono::minutes{30}))
                  .Value()} {}

    core::Result<PasswordRules> Passwords() const override {
        return passwords_;
    }
    core::Result<ThrottleLimits> Throttle() const override {
        return throttle_;
    }
    core::Result<AuthLifetimes> Lifetimes() const override {
        return lifetimes_;
    }

    void SetThrottle(ThrottleLimits limits) {
        throttle_ = limits;
    }
    void SetLifetimes(AuthLifetimes lifetimes) {
        lifetimes_ = lifetimes;
    }

private:
    PasswordRules passwords_;
    ThrottleLimits throttle_;
    AuthLifetimes lifetimes_;
};

}  // namespace pdr::identity::testing
