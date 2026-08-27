#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "identity/application/contract_service.hpp"
#include "identity/application/policies/policy_set.hpp"
#include "identity/application/policies/subject_builder.hpp"
#include "identity/application/ports/access_journal.hpp"
#include "identity/application/ports/access_log.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/configuration_faults.hpp"
#include "identity/application/ports/guardian_consents.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/maturity_settings.hpp"
#include "identity/application/ports/role_repository.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity::testing {

/// Роли в памяти: столько, сколько нужно тесту прав.
class FakeRoles final : public ports::RoleRepository {
public:
    void Grant(const core::TenantId& tenant, const core::PersonId& person, Role role) {
        auto& set = rows_[Key(tenant, person)];
        set = set.With(role);
    }

    RoleSet RolesOf(const core::TenantId& tenant, const core::PersonId& person) const override {
        const auto found = rows_.find(Key(tenant, person));
        return found == rows_.end() ? RoleSet{} : found->second;
    }

private:
    static std::string Key(const core::TenantId& tenant, const core::PersonId& person) {
        return tenant.ToString() + "|" + person.ToString();
    }

    std::unordered_map<std::string, RoleSet> rows_;
};

/// Поломки настройки, о которых можно спросить: что именно сообщили и сколько раз.
class FakeFaults final : public ports::ConfigurationFaults {
public:
    void NoPolicyFor(Action action) const override {
        reported_.push_back(action);
    }

    const std::vector<Action>& Reported() const noexcept {
        return reported_;
    }

private:
    mutable std::vector<Action> reported_;
};

/// Согласия в памяти. Хранит и отозванные: «был доступ» и «есть доступ» —
/// разные вопросы, и фейк, выбрасывающий отозванные, скрыл бы половину.
class FakeConsents final : public ports::GuardianConsents {
public:
    std::vector<GuardianConsent> ActiveFor(const core::TenantId& tenant,
                                           const core::PersonId& guardian,
                                           const core::PersonId& student) const override {
        std::vector<GuardianConsent> found;
        for (const auto& consent : rows_) {
            if (consent.Tenant() == tenant && consent.Guardian() == guardian &&
                consent.Student() == student && !consent.RevokedAt().has_value()) {
                found.push_back(consent);
            }
        }
        return found;
    }

    std::optional<GuardianConsent> FindActive(const core::TenantId& tenant,
                                              const core::PersonId& guardian,
                                              const core::PersonId& student,
                                              GuardianScope scope) const override {
        for (const auto& consent : ActiveFor(tenant, guardian, student)) {
            if (consent.Scope() == scope) {
                return consent;
            }
        }
        return std::nullopt;
    }

    void Save(const GuardianConsent& consent) override {
        for (auto& stored : rows_) {
            if (stored.Id() == consent.Id()) {
                stored = consent;
                return;
            }
        }
        rows_.push_back(consent);
    }

    const std::vector<GuardianConsent>& Rows() const noexcept {
        return rows_;
    }

private:
    std::vector<GuardianConsent> rows_;
};

class FakeBirthDates final : public ports::BirthDates {
public:
    void Put(const core::TenantId& tenant, const core::PersonId& person, BirthDate born_on) {
        rows_.insert_or_assign(tenant.ToString() + "|" + person.ToString(), born_on);
    }

    std::optional<BirthDate> Of(const core::TenantId& tenant,
                                const core::PersonId& person) const override {
        const auto found = rows_.find(tenant.ToString() + "|" + person.ToString());
        return found == rows_.end() ? std::optional<BirthDate>{} : found->second;
    }

private:
    std::unordered_map<std::string, BirthDate> rows_;
};

/// Правило совершеннолетия, которое можно двигать прямо в тесте.
class FakeMaturity final : public ports::MaturitySettings {
public:
    /// Умолчания реестра: 14, 16, 18 и месяц на решение.
    FakeMaturity()
        : rule_{MaturityRule::Compose(AgeThresholds::Compose(14, 16, 18).Value(),
                                      std::chrono::duration_cast<core::Instant::Duration>(
                                          std::chrono::hours{24 * 30}))
                    .Value()} {}

    core::Result<MaturityRule> Rule() const override {
        return rule_;
    }

    void Set(MaturityRule rule) {
        rule_ = rule;
    }

private:
    MaturityRule rule_;
};

/// Журнал доступа: пишущая и читающая стороны одного хранилища.
///
/// В проде это разные порты и разные адаптеры; здесь одна память, потому что
/// проверяется как раз то, что записанное читается.
class FakeJournal final : public ports::AccessLog, public ports::AccessJournal {
public:
    void Record(const AccessRecord& record) override {
        rows_.push_back(record);
    }

    std::vector<AccessRecord> AboutPerson(const core::TenantId& tenant,
                                          const core::PersonId& subject,
                                          core::Instant since) const override {
        std::vector<AccessRecord> found;
        for (const auto& record : rows_) {
            if (record.Tenant() == tenant && record.Subject() == subject && record.At() >= since) {
                found.push_back(record);
            }
        }
        return found;
    }

    const std::vector<AccessRecord>& Rows() const noexcept {
        return rows_;
    }

private:
    std::vector<AccessRecord> rows_;
};

/// Опека, которой либо нет, либо она между этими двумя.
class FakeGuardianship final : public ports::GuardianshipRepository {
public:
    void Establish(core::TenantId tenant, core::PersonId guardian, core::PersonId student) {
        rows_.push_back(Guardianship::Restore(std::move(tenant),
                                              std::move(guardian),
                                              std::move(student),
                                              core::Instant::FromUnixMicros(0),
                                              std::nullopt));
    }

    std::optional<Guardianship> FindActive(const core::TenantId& tenant,
                                           const core::PersonId& guardian,
                                           const core::PersonId& student) const override {
        for (const auto& row : rows_) {
            if (row.Tenant() == tenant && row.Guardian() == guardian && row.Student() == student &&
                row.IsActive()) {
                return row;
            }
        }
        return std::nullopt;
    }

    std::vector<core::PersonId> GuardiansOf(const core::TenantId& tenant,
                                            const core::PersonId& student) const override {
        std::vector<core::PersonId> found;
        for (const auto& row : rows_) {
            if (row.Tenant() == tenant && row.Student() == student && row.IsActive()) {
                found.push_back(row.Guardian());
            }
        }
        return found;
    }

    void Save(const Guardianship& guardianship) override {
        for (auto& row : rows_) {
            if (row.Guardian() == guardianship.Guardian() &&
                row.Student() == guardianship.Student()) {
                row = guardianship;
                return;
            }
        }
        rows_.push_back(guardianship);
    }

private:
    std::vector<Guardianship> rows_;
};

/// Весь набор для проверки прав: люди, согласия, политики, журнал.
///
/// Собран целиком затем, что собирать его по семь строк в каждом тесте — способ
/// забыть один порт и не заметить. Порядок полей — порядок построения: сначала
/// хранилища, потом то, что на них стоит.
struct AccessWorld final {
    FakeRoles roles;
    FakeFaults faults;
    FakeConsents consents;
    FakeBirthDates birth_dates;
    FakeMaturity maturity;
    FakeJournal journal;
    FakeGuardianship guardianships;
    pdr::testing::FakeClock clock;
    pdr::testing::FakeIdGenerator ids;

    policies::PolicySet permissions{faults};
    NoteSensitiveAccess notes{journal, clock};
    policies::SubjectBuilder subjects{guardianships, roles, consents, birth_dates, maturity, clock};
    ContractService contract{subjects, permissions, notes};

    /// Открыть уровень напрямую, минуя сценарий: тесту прав незачем каждый раз
    /// проходить проверки выдачи.
    void Open(const core::TenantId& tenant,
              const core::PersonId& guardian,
              const core::PersonId& student,
              GuardianScope scope,
              const core::PersonId& granted_by) {
        static std::uint64_t next = 1;
        core::IdBytes bytes{};
        bytes.back() = static_cast<std::uint8_t>(next++);
        consents.Save(GuardianConsent::Grant(ConsentId::FromBytes(bytes),
                                             tenant,
                                             guardian,
                                             student,
                                             scope,
                                             granted_by,
                                             clock.Now(),
                                             std::nullopt)
                          .Value());
    }
};

}  // namespace pdr::identity::testing
