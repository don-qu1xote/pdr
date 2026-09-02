#include "fakes/fake_tenant_aware_repository.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace pdr::testing {

FakeTenantSession::FakeTenantSession(std::vector<FakeRow>& rows,
                                     core::TenantId tenant,
                                     application::ports::Intent intent) noexcept
    : rows_{rows}, tenant_{tenant}, intent_{intent} {}

void FakeTenantSession::RefuseIfReading(std::string_view what) const {
    if (intent_ == application::ports::Intent::kReading) {
        throw std::logic_error{"область объявлена читающей, а работа делает «" + std::string{what} +
                               "»: настоящая читающая транзакция отвечает тем же"};
    }
}

std::vector<std::string> FakeTenantSession::SelectAll() const {
    std::vector<std::string> visible;
    for (const auto& row : rows_) {
        if (row.tenant == tenant_) {
            visible.push_back(row.payload);
        }
    }
    return visible;
}

void FakeTenantSession::Insert(std::string payload) {
    RefuseIfReading("insert");
    rows_.push_back(FakeRow{tenant_, std::move(payload)});
}

bool FakeTenantSession::InsertFor(const core::TenantId& owner, std::string payload) {
    RefuseIfReading("insert");
    if (owner != tenant_) {
        return false;
    }
    rows_.push_back(FakeRow{owner, std::move(payload)});
    return true;
}

std::size_t FakeTenantSession::DeleteAll() {
    RefuseIfReading("delete");
    const auto removed = std::remove_if(
        rows_.begin(), rows_.end(), [this](const FakeRow& row) { return row.tenant == tenant_; });
    const auto count = static_cast<std::size_t>(std::distance(removed, rows_.end()));
    rows_.erase(removed, rows_.end());
    return count;
}

void FakeTenantSession::OnRollback(std::function<void()> undo) {
    undo_.push_back(std::move(undo));
}

void FakeTenantSession::Unwind() {
    for (auto step = undo_.rbegin(); step != undo_.rend(); ++step) {
        (*step)();
    }
    undo_.clear();
}

void FakeTenantAwareRepository::Run(application::ports::Intent intent,
                                    const core::TenantId& tenant,
                                    const Work& work) {
    ++declarations_;
    last_intent_ = intent;

    auto before = rows_;
    FakeTenantSession session{rows_, tenant, intent};
    try {
        work(session);
    } catch (...) {
        session.Unwind();
        rows_ = std::move(before);
        throw;
    }
}

}  // namespace pdr::testing
