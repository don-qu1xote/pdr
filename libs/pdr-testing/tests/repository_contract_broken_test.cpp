#include <cstddef>
#include <string>
#include <vector>

#include <pdr/testing/repository_contract.hpp>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"

/// @file
/// ОТРИЦАТЕЛЬНЫЙ СЛУЧАЙ ко всей затее: сломанный фейк обязан ронять
/// contract-прогон.
///
/// Contract-набор ценен ровно настолько, насколько он ловит расхождение фейка с
/// адаптером. Проверить это можно единственным способом: подсунуть фейк, который
/// ведёт себя неправильно, и убедиться, что набор упал. Поэтому цель
/// pdr_broken_fake_contract_test помечена в ctest как WILL_FAIL: зелёный прогон
/// здесь означает, что contract-набор ничего не проверяет.
///
/// Сломан он единственным способом и намеренно грубо: не фильтрует строки по
/// арендатору. Это ровно та ошибка, из-за которой «на фейке работало», а в проде
/// оказалась утечка между арендаторами.

namespace pdr::testing {
namespace {

struct LeakyRow final {
    core::TenantId tenant;
    std::string payload;
};

class LeakySession final {
public:
    LeakySession(std::vector<LeakyRow>& rows, core::TenantId tenant) noexcept
        : rows_{rows}, tenant_{tenant} {}

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }

    /// ЗДЕСЬ ПОЛОМКА: показывает всё, включая чужое.
    std::vector<std::string> SelectAll() const {
        std::vector<std::string> all;
        for (const auto& row : rows_) {
            all.push_back(row.payload);
        }
        return all;
    }

    void Insert(std::string payload) {
        rows_.push_back(LeakyRow{tenant_, std::move(payload)});
    }

    bool InsertFor(const core::TenantId& owner, std::string payload) {
        rows_.push_back(LeakyRow{owner, std::move(payload)});
        return true;
    }

    std::size_t DeleteAll() {
        const auto removed = rows_.size();
        rows_.clear();
        return removed;
    }

private:
    std::vector<LeakyRow>& rows_;
    core::TenantId tenant_;
};

class LeakyRepository final : public application::ports::TenantAwareRepository<LeakySession> {
public:
    std::size_t Rows() const noexcept {
        return rows_.size();
    }

private:
    void Run(application::ports::Intent intent,
             const core::TenantId& tenant,
             const Work& work) override {
        static_cast<void>(intent);
        LeakySession session{rows_, tenant};
        work(session);
    }

    std::vector<LeakyRow> rows_;
};

struct LeakyWorld final {
    using Session = LeakySession;

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
        return repository_.Rows();
    }

private:
    LeakyRepository repository_;
};

}  // namespace

PDR_REPOSITORY_CONTRACT(Leaky, LeakyWorld);

}  // namespace pdr::testing
