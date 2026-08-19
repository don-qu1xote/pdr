#include "core/types/ids.hpp"

#include <map>
#include <string>
#include <unordered_map>

#include "testing/check.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using pdr::core::PersonId;
using pdr::core::TenantId;

constexpr std::string_view kText = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";

void ParsesCanonicalText() {
    const auto person = PersonId::Parse(kText);
    PDR_CHECK(person.has_value());
    PDR_CHECK(person->ToString() == std::string{kText});

    // Разбор не зависит от регистра, вывод всегда в нижнем.
    const auto upper = PersonId::Parse("3F2504E0-4F89-11D3-9A0C-0305E82C3301");
    PDR_CHECK(upper.has_value());
    PDR_CHECK(*upper == *person);
}

void RejectsWhatIsNotAnIdentifier() {
    PDR_CHECK(!PersonId::Parse("").has_value());
    PDR_CHECK(!PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c330").has_value());
    PDR_CHECK(!PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c33011").has_value());
    PDR_CHECK(!PersonId::Parse("3f2504e04f89-11d3-9a0c-0305e82c3301-").has_value());
    PDR_CHECK(!PersonId::Parse("3f2504e0-4f89-11d3-9a0c-0305e82c330z").has_value());
    PDR_CHECK(!PersonId::Parse("не идентификатор").has_value());
}

void ComparesAndKeysCollections() {
    const auto first = PersonId::Parse(kText);
    const auto second = PersonId::Parse("00000000-0000-0000-0000-000000000002");
    PDR_CHECK(first.has_value() && second.has_value());
    PDR_CHECK(*first != *second);
    PDR_CHECK((*second < *first));

    std::map<PersonId, int> ordered;
    ordered.emplace(*first, 1);
    ordered.emplace(*second, 2);
    PDR_CHECK(ordered.size() == 2);
    PDR_CHECK(ordered.begin()->second == 2);

    std::unordered_map<PersonId, int> hashed;
    hashed.emplace(*first, 1);
    hashed.emplace(*second, 2);
    PDR_CHECK(hashed.size() == 2);
    PDR_CHECK(hashed.at(*first) == 1);
}

void GeneratorGivesTypedIdentifiers() {
    const pdr::testing::FakeIdGenerator generator;

    const auto person = generator.Next<PersonId>();
    const auto tenant = generator.Next<TenantId>();

    PDR_CHECK(person.ToString() == "00000000-0000-0000-0000-000000000001");
    PDR_CHECK(tenant.ToString() == "00000000-0000-0000-0000-000000000002");
    PDR_CHECK(generator.Issued() == 2);

    // Разные типы: сравнить person и tenant нечем, и это проверяется отдельно —
    // тестами compile_fail/, которые обязаны НЕ собираться.
    PDR_CHECK(person != generator.Next<PersonId>());
}

}  // namespace

int main() {
    ParsesCanonicalText();
    RejectsWhatIsNotAnIdentifier();
    ComparesAndKeysCollections();
    GeneratorGivesTypedIdentifiers();
    return pdr::testing::Summary("core.ids");
}
