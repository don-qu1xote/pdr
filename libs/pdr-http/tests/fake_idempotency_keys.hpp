#pragma once

#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "application/ports/idempotency_keys.hpp"
#include "fakes/fake_tenant_aware_repository.hpp"

namespace pdr::http::testing {

/// Фейк ключей идемпотентности с ТОЙ ЖЕ атомарностью, что у базы.
///
/// Мьютекс здесь — не то, что запрещено задачей. Запрещён мьютекс В БОЕВОМ
/// КОДЕ вместо блокировки в базе: он не знает про вторую реплику. Здесь он
/// изображает то, что в базе делает первичный ключ, — и без него фейк
/// зеленил бы ровно те проверки, ради которых всё написано: два потока
/// получили бы «ключ свободен» и выполнили операцию дважды.
///
/// Сессия принимается параметром и не используется: фейк хранилища арендатора
/// уже провёл нас через область, а второй копии политики здесь не нужно.
class FakeIdempotencyKeys final : public ports::IdempotencyKeys<pdr::testing::FakeTenantSession> {
public:
    /// Строка таблицы, а не запись кэша: те же поля, включая срок. Срок здесь
    /// лежит и не применяется — ровно как в базе, где просроченный ключ
    /// продолжает держать повтор, пока его не унесёт уборка. Фейк, который
    /// освобождал бы ключ сам, зеленил бы поведение, которого в проде нет.
    struct Row final {
        RequestFingerprint fingerprint;
        KeyState state;
        SavedAnswer answer;
        core::Instant expires_at;
    };

    std::size_t Rows() const {
        const std::lock_guard guard{lock_};
        return rows_.size();
    }

    int Taken() const noexcept {
        return taken_;
    }

private:
    using Id = std::pair<std::string, std::string>;

    core::Result<Claim> Take(pdr::testing::FakeTenantSession& session,
                             const core::TenantId& tenant,
                             const IdempotencyKey& key,
                             const RequestFingerprint& fingerprint,
                             core::Instant expires_at) override {
        const std::lock_guard guard{lock_};
        const Id id{tenant.ToString(), key.Value()};

        const auto found = rows_.find(id);
        if (found == rows_.end()) {
            rows_.emplace(id, Row{fingerprint, KeyState::kInProgress, {}, expires_at});
            ++taken_;
            session.OnRollback([this, id] {
                const std::lock_guard undoing{lock_};
                rows_.erase(id);
            });
            return Claim{ClaimOutcome::kTaken, {}};
        }

        if (!(found->second.fingerprint == fingerprint)) {
            return core::Error{core::ErrorKind::kConflict,
                               "idempotency_key_reused",
                               "этот ключ уже занят запросом с другим телом"};
        }
        if (found->second.state == KeyState::kInProgress) {
            return Claim{ClaimOutcome::kInFlight, {}};
        }
        return Claim{ClaimOutcome::kReplay, found->second.answer};
    }

    core::Result<void> Complete(pdr::testing::FakeTenantSession& session,
                                const core::TenantId& tenant,
                                const IdempotencyKey& key,
                                const SavedAnswer& answer) override {
        (void)session;

        const std::lock_guard guard{lock_};
        auto& row = rows_.at(Id{tenant.ToString(), key.Value()});
        row.state = KeyState::kCompleted;
        row.answer = answer;
        return {};
    }

    mutable std::mutex lock_;
    std::map<Id, Row> rows_;  // штатное-ok: idempotency-keys-fake — двойник таблицы, а не кэш
    int taken_{0};
};

}  // namespace pdr::http::testing
