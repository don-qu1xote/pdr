#include "core/types/ids.hpp"

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_id_generator.hpp"

namespace pdr::core {
namespace {

constexpr std::string_view kText = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";

TEST(StrongId, ParsesCanonicalText) {
    const auto person = PersonId::Parse(kText);
    ASSERT_TRUE(person.has_value());
    EXPECT_EQ(person->ToString(), std::string{kText});

    // Разбор не зависит от регистра, вывод всегда в нижнем.
    const auto upper = PersonId::Parse("3F2504E0-4F89-11D3-9A0C-0305E82C3301");
    ASSERT_TRUE(upper.has_value());
    EXPECT_TRUE(*upper == *person);
}

TEST(StrongId, RejectsWhatIsNotAnIdentifier) {
    EXPECT_FALSE(PersonId::Parse("").has_value());
    EXPECT_FALSE(PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c330").has_value());
    EXPECT_FALSE(PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c33011").has_value());
    EXPECT_FALSE(PersonId::Parse("3f2504e04f89-11d3-9a0c-0305e82c3301-").has_value());
    EXPECT_FALSE(PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c330z").has_value());
    EXPECT_FALSE(PersonId::Parse("не идентификатор").has_value());
}

TEST(StrongId, ComparesAndKeysCollections) {
    const auto first = PersonId::Parse(kText);
    const auto second = testing::Numbered<PersonId>(2);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(*first == second);
    EXPECT_TRUE(second < *first);

    std::map<PersonId, int> ordered;
    ordered.emplace(*first, 1);
    ordered.emplace(second, 2);
    ASSERT_EQ(ordered.size(), 2U);
    EXPECT_EQ(ordered.begin()->second, 2);

    std::unordered_map<PersonId, int> hashed;
    hashed.emplace(*first, 1);
    hashed.emplace(second, 2);
    ASSERT_EQ(hashed.size(), 2U);
    EXPECT_EQ(hashed.at(*first), 1);
}

TEST(StrongId, GeneratorGivesTypedIdentifiers) {
    const pdr::testing::FakeIdGenerator generator;

    const auto person = generator.Next<PersonId>();
    const auto tenant = generator.Next<TenantId>();

    EXPECT_EQ(person.ToString(), "00000000-0000-0000-0000-000000000001");
    EXPECT_EQ(tenant.ToString(), "00000000-0000-0000-0000-000000000002");
    EXPECT_EQ(generator.Issued(), 2U);

    // Разные типы: сравнить person и tenant нечем, и это проверяется отдельно —
    // тестами compile_fail/, которые обязаны НЕ собираться.
    EXPECT_FALSE(person == generator.Next<PersonId>());
}

}  // namespace
}  // namespace pdr::core
