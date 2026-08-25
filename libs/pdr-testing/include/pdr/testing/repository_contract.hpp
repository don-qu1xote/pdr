#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "core/types/ids.hpp"

/// @file
/// Contract-набор порта хранилища с арендатором: ОДИН набор проверок,
/// инстанцируемый для каждой реализации порта.
///
/// Зачем он есть. Unit-тесты стоят на фейках. Если фейк ведёт себя не так, как
/// настоящий адаптер, зелёный unit-прогон не значит ничего: он проверял
/// поведение, которого в проде нет. Это подстановка Лисков, и проверяется она
/// единственным способом — одними и теми же утверждениями против обеих
/// реализаций. Два скопированных файла эту работу не делают: они расходятся в
/// первый же день, когда правку внесли в один.
///
/// Набор type-parameterized (`TYPED_TEST_SUITE_P`) именно затем, чтобы
/// инстанцирований было несколько и они лежали в разных целях сборки: фейк
/// проверяется без базы, настоящий адаптер — там, где база есть.
///
/// Настоящий адаптер живёт в сопрограммах userver, поэтому набор умеет
/// разворачиваться и в UTEST-макросы: цель, которая линкует `userver::utest`,
/// переопределяет четыре макроса ниже до включения этого файла. По умолчанию —
/// обычный gtest, без userver и без базы.
///
/// Как подключить реализацию: объявить «мир» и позвать PDR_REPOSITORY_CONTRACT.
///
/// @code
/// struct MyWorld {
///     using Session = ...;                                    // сессия области
///     application::ports::TenantAwareRepository<Session>& Repository();
///     static void Insert(Session&, std::string);               // своя строка
///     static bool InsertFor(Session&, const core::TenantId&, std::string);
///     static std::vector<std::string> SelectAll(Session&);     // без условия
///     static std::size_t DeleteAll(Session&);                 // без условия
///     static core::TenantId Declared(Session&);   // какой арендатор объявлен
///     std::size_t RowsBypassingPolicy();          // сколько строк всего
/// };
///
/// PDR_REPOSITORY_CONTRACT(Fake, MyWorld);
/// @endcode

#ifndef PDR_CONTRACT_SUITE_P
#define PDR_CONTRACT_SUITE_P(suite) TYPED_TEST_SUITE_P(suite)
#endif

#ifndef PDR_CONTRACT_TEST_P
#define PDR_CONTRACT_TEST_P(suite, name) TYPED_TEST_P(suite, name)
#endif

#ifndef PDR_CONTRACT_REGISTER_P
#define PDR_CONTRACT_REGISTER_P(suite, ...) REGISTER_TYPED_TEST_SUITE_P(suite, __VA_ARGS__)
#endif

#ifndef PDR_CONTRACT_INSTANTIATE_P
#define PDR_CONTRACT_INSTANTIATE_P(prefix, suite, types) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, suite, types)
#endif

namespace pdr::testing {

/// Оснастка набора: два арендатора и мир реализации. Общей базовой фикстуры «на
/// всё» в проекте нет — эта знает ровно про один порт.
template<class World>
class RepositoryContract : public ::testing::Test {
protected:
    static core::TenantId Tenant(unsigned char marker) {
        core::IdBytes bytes{};
        bytes[0] = marker;
        return core::TenantId::FromBytes(bytes);
    }

    core::TenantId First() const {
        return Tenant(1);
    }

    core::TenantId Second() const {
        return Tenant(2);
    }

    World world_;
};

PDR_CONTRACT_SUITE_P(RepositoryContract);

/// ГЛАВНОЕ: строка одного арендатора не видна под другим ни одним запросом,
/// включая выборку без условия.
PDR_CONTRACT_TEST_P(RepositoryContract, NothingOfAnotherTenantIsVisible) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();

    repository.InTenant(this->First(),
                        [](Session& session) { TypeParam::Insert(session, "занятие А"); });
    repository.InTenant(this->Second(),
                        [](Session& session) { TypeParam::Insert(session, "занятие Б"); });

    const auto seen = repository.InTenant(
        this->Second(), [](Session& session) { return TypeParam::SelectAll(session); });

    ASSERT_EQ(seen.size(), 1U) << "из области второго арендатора видно не только его строку";
    EXPECT_EQ(seen.front(), "занятие Б");
    EXPECT_EQ(this->world_.RowsBypassingPolicy(), 2U);
}

/// Чужая строка не находится и по значению: «не видно» — это про все запросы, а
/// не про выборку без условия.
PDR_CONTRACT_TEST_P(RepositoryContract, ForeignRowIsNotFoundByItsPayload) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();

    repository.InTenant(this->First(),
                        [](Session& session) { TypeParam::Insert(session, "секрет"); });

    const auto found = repository.InTenant(this->Second(), [](Session& session) {
        const auto rows = TypeParam::SelectAll(session);
        return std::count(rows.begin(), rows.end(), std::string{"секрет"});
    });

    EXPECT_EQ(found, 0) << "чужая строка находится по значению";
}

/// Арендатор объявлен раньше, чем работа задала первый вопрос.
PDR_CONTRACT_TEST_P(RepositoryContract, TenantIsDeclaredBeforeAnyQuery) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();

    const auto declared = repository.InTenant(
        this->First(), [](Session& session) { return TypeParam::Declared(session); });

    EXPECT_TRUE(declared == this->First()) << "область начала работу не под тем арендатором";
}

/// Вставка с чужим арендатором — отказ, а не строка, которую вставивший потом не
/// увидит. В базе это `with check` политики.
PDR_CONTRACT_TEST_P(RepositoryContract, ForeignRowCannotBeWritten) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();
    const auto foreign = this->Second();

    const auto accepted = repository.InTenant(this->First(), [&foreign](Session& session) {
        return TypeParam::InsertFor(session, foreign, "чужое");
    });

    EXPECT_FALSE(accepted) << "строка с чужим арендатором принята";
    EXPECT_EQ(this->world_.RowsBypassingPolicy(), 0U);
}

/// Изменение без условия трогает только своё: `delete from ...` под одним
/// арендатором не вычищает хранилище целиком.
PDR_CONTRACT_TEST_P(RepositoryContract, BareDeleteTouchesOnlyOwnRows) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();

    repository.InTenant(this->First(), [](Session& session) {
        TypeParam::Insert(session, "первое");
        TypeParam::Insert(session, "второе");
    });
    repository.InTenant(this->Second(),
                        [](Session& session) { TypeParam::Insert(session, "чужое"); });

    const auto removed = repository.InTenant(
        this->First(), [](Session& session) { return TypeParam::DeleteAll(session); });
    EXPECT_EQ(removed, 2U) << "удаление без условия задело не свои строки";

    const auto left = repository.InTenant(
        this->Second(), [](Session& session) { return TypeParam::SelectAll(session); });
    ASSERT_EQ(left.size(), 1U);
    EXPECT_EQ(left.front(), "чужое");
    EXPECT_EQ(this->world_.RowsBypassingPolicy(), 1U);
}

/// Область возвращает то, что вернула работа, и ничего не требует от работы без
/// результата. Механика обёртки одинакова у всех реализаций, поэтому она здесь.
PDR_CONTRACT_TEST_P(RepositoryContract, ScopeReturnsWhatTheWorkReturns) {
    using Session = typename TypeParam::Session;
    auto& repository = this->world_.Repository();

    const auto counted = repository.InTenant(
        this->First(), [](Session& session) { return TypeParam::SelectAll(session).size(); });
    EXPECT_EQ(counted, 0U);

    static_assert(std::is_void_v<decltype(repository.InTenant(
                      core::TenantId::FromBytes(core::IdBytes{}),
                      [](Session& session) { TypeParam::Insert(session, "x"); }))>,
                  "работа без результата не должна заставлять сценарий что-то возвращать");
}

PDR_CONTRACT_REGISTER_P(RepositoryContract,
                        NothingOfAnotherTenantIsVisible,
                        ForeignRowIsNotFoundByItsPayload,
                        TenantIsDeclaredBeforeAnyQuery,
                        ForeignRowCannotBeWritten,
                        BareDeleteTouchesOnlyOwnRows,
                        ScopeReturnsWhatTheWorkReturns);

}  // namespace pdr::testing

/// Инстанцировать набор для своей реализации. Приставка попадает в имена
/// прогонов, поэтому по выводу видно, какая реализация не выполнила контракт.
#define PDR_REPOSITORY_CONTRACT(prefix, world) \
    PDR_CONTRACT_INSTANTIATE_P(prefix, RepositoryContract, ::testing::Types<world>)
