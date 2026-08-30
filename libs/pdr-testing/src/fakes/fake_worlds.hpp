#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/types/ids.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "fakes/fake_tenant_aware_repository.hpp"

namespace pdr::testing {

/// «Миры» фейков для contract-наборов: как создать реализацию и как задать ей
/// вопросы набора на её языке.
///
/// Лежат в оснастке, а не в файле теста, ровно по той же причине, по которой
/// фейк один на проект: иначе у каждого прогона окажется своё представление о
/// том, что значит «вставить строку».

struct FakeRepositoryWorld final {
    using Session = FakeTenantSession;

    application::ports::TenantAwareRepository<Session>& Repository() noexcept {
        return repository_;
    }

    static void Insert(Session& session, std::string payload) {
        session.Insert(std::move(payload));
    }

    static bool InsertFor(Session& session, const core::TenantId& owner, std::string payload) {
        return session.InsertFor(owner, std::move(payload));
    }

    static std::vector<std::string> SelectAll(Session& session) {
        return session.SelectAll();
    }

    static std::size_t DeleteAll(Session& session) {
        return session.DeleteAll();
    }

    static core::TenantId Declared(Session& session) {
        return session.Tenant();
    }

    std::size_t RowsBypassingPolicy() {
        return repository_.RowsBypassingPolicy().size();
    }

private:
    FakeTenantAwareRepository repository_;
};

struct FakeClockWorld final {
    /// Фейк стоит на месте, пока его не двинули: в этом весь смысл подмены.
    static constexpr bool kMovesOnItsOwn = false;

    const application::ports::Clock& Clock() const noexcept {
        return clock_;
    }

private:
    FakeClock clock_;
};

struct FakeIdGeneratorWorld final {
    const application::ports::IdGenerator& Generator() const noexcept {
        return generator_;
    }

private:
    FakeIdGenerator generator_;
};

struct FakeSecretGeneratorWorld final {
    const application::ports::SecretGenerator& Secrets() const noexcept {
        return secrets_;
    }

private:
    FakeSecretGenerator secrets_;
};

}  // namespace pdr::testing
