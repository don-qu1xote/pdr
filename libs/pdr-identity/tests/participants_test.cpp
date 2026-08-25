#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/person.hpp"
#include "identity/core/tenant.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

Tenant SomeTenant(int number = 1) {
    const auto tenant = Tenant::Compose(Numbered<core::TenantId>(number), "Мария Петровна");
    return tenant.Value();
}

Person SomeStudent(int number = 20) {
    return Person{Numbered<core::PersonId>(number),
                  Email::Parse("student@example.test").Value(),
                  BirthDate::Of(2011, 3, 4).Value()};
}

TEST(Tenant, NameIsRequiredAndTrimmed) {
    const auto named = Tenant::Compose(Numbered<core::TenantId>(1), "  Школа у дома  ");

    ASSERT_TRUE(named.HasValue());
    EXPECT_EQ(named.Value().Name(), "Школа у дома");
}

TEST(Tenant, BlankNameIsRefused) {
    for (const std::string blank : {"", " ", "\t\n"}) {
        const auto refused = Tenant::Compose(Numbered<core::TenantId>(1), blank);

        ASSERT_FALSE(refused.HasValue()) << "пустое имя «" << blank << "» прошло";
        EXPECT_EQ(refused.Failure().Code(), "tenant_name_blank");
        EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kValidation);
    }
}

TEST(Email, IsLoweredSoThatOnePersonStaysOnePerson) {
    const auto shouting = Email::Parse("Ivan.Petrov@Example.TEST");
    const auto quiet = Email::Parse("ivan.petrov@example.test");

    ASSERT_TRUE(shouting.HasValue());
    ASSERT_TRUE(quiet.HasValue());
    EXPECT_EQ(shouting.Value().Value(), "ivan.petrov@example.test");
    EXPECT_TRUE(shouting.Value() == quiet.Value());
}

TEST(Email, MalformedIsRefused) {
    for (const std::string bad : {"",
                                  "без-собаки.example.test",
                                  "два@@example.test",
                                  "@example.test",
                                  "ivan@",
                                  "ivan@example",
                                  "иван петров@example.test"}) {
        const auto refused = Email::Parse(bad);

        ASSERT_FALSE(refused.HasValue()) << "адрес «" << bad << "» прошёл";
        EXPECT_EQ(refused.Failure().Code(), "email_malformed");
    }
}

TEST(Person, KeepsDateOfBirthAndNotAge) {
    const auto student = SomeStudent();

    EXPECT_EQ(student.BornOn().ToString(), "2011-03-04");
    EXPECT_EQ(student.Mail().Value(), "student@example.test");
}

TEST(Person, MailIsChangedByANewValueAndNotBySetter) {
    const auto student = SomeStudent();

    const auto moved = student.WithMail(Email::Parse("new@example.test").Value());

    EXPECT_EQ(moved.Mail().Value(), "new@example.test");
    EXPECT_TRUE(moved.Id() == student.Id());
    EXPECT_EQ(student.Mail().Value(), "student@example.test") << "прежнее значение не тронуто";
}

TEST(TenantMembership, RoleCodesAreTheOnesTheSchemaKnows) {
    EXPECT_EQ(Name(Role::kOwner), "owner");
    EXPECT_EQ(Name(Role::kTutor), "tutor");
    EXPECT_EQ(Name(Role::kStudent), "student");
    EXPECT_EQ(Name(Role::kGuardian), "guardian");

    EXPECT_EQ(ParseRole("guardian"), Role::kGuardian);
    EXPECT_EQ(ParseRole("родитель"), std::nullopt);
    EXPECT_EQ(ParseRole("Owner"), std::nullopt) << "регистр не тот — это другое слово";
}

/// Два участия одного человека в ОДНОМ арендаторе: репетитор, который сам же
/// приводит сюда своего ребёнка. Это норма, а не краевой случай, и модель ей
/// не мешает — участий столько, сколько ролей.
TEST(TenantMembership, OnePersonHoldsTwoRolesInTheSameTenant) {
    const auto tenant = SomeTenant();
    const auto person = Numbered<core::PersonId>(7);

    const std::vector<TenantMembership> held{
        TenantMembership::In(tenant, person, Role::kTutor),
        TenantMembership::In(tenant, person, Role::kGuardian),
    };

    EXPECT_TRUE(held.front().TenantId() == tenant.Id());
    EXPECT_TRUE(held.front().Person() == held.back().Person());
    EXPECT_NE(held.front().InRole(), held.back().InRole());
    EXPECT_FALSE(held.front() == held.back()) << "две роли — два разных участия";
}

/// Тот же человек в чужом арендаторе — другое участие с другой ролью.
TEST(TenantMembership, SamePersonIsSomebodyElseInAnotherTenant) {
    const auto own = SomeTenant(1);
    const auto foreign = SomeTenant(2);
    const auto person = Numbered<core::PersonId>(7);

    const auto tutor = TenantMembership::In(own, person, Role::kTutor);
    const auto guardian = TenantMembership::In(foreign, person, Role::kGuardian);

    EXPECT_FALSE(tutor.SameTenantAs(guardian));
    EXPECT_TRUE(tutor.Person() == guardian.Person());
}

TEST(TenantMembership, RestoredFromStorageKeepsWhatWasStored) {
    const auto restored = TenantMembership::Restore(
        Numbered<core::TenantId>(3), Numbered<core::PersonId>(9), Role::kOwner);

    EXPECT_TRUE(restored.TenantId() == Numbered<core::TenantId>(3));
    EXPECT_EQ(restored.InRole(), Role::kOwner);
}

}  // namespace
}  // namespace pdr::identity
