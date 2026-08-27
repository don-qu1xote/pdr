#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "identity/application/policies/matrix.hpp"
#include "identity/core/capabilities.hpp"

namespace pdr::identity::policies {
namespace {

/// Пороги для сборки документа — те же умолчания, что в реестре значений.
/// Матрица возраста собирается из них, а не из констант в разметке.
AgeThresholds Thresholds() {
    return AgeThresholds::Compose(14, 16, 18).Value();
}

std::string InDocs() {
    return std::string{PDR_SOURCE_DIR} + "/docs/architecture/permissions.md";
}

/// Куда кладётся заново собранный документ. Перезапись матрицы —
/// `make permissions-lock`: она копирует этот файл поверх того, что в docs.
/// Писать в дерево из теста не нужно, а лишний ключ окружения тем более:
/// переменные окружения в этом проекте несут путь к секретам и ничего больше
/// (`scripts/check_secrets.py`).
std::string InBuild() {
    return std::string{PDR_BINARY_DIR} + "/permissions.md";
}

std::string Read(const std::string& path) {
    std::ifstream file{path};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

TEST(PermissionsMatrix, EveryActionAndRoleIsInTheTable) {
    const testing::FakeFaults faults;
    const PolicySet permissions{faults};

    const auto table = RenderMatrix(permissions);

    for (const auto action : kEveryAction) {
        EXPECT_NE(table.find(std::string{"`"} + std::string{Name(action)} + "`"), std::string::npos)
            << "действия «" << Name(action) << "» нет в матрице";
    }
    for (const auto role : kEveryRole) {
        EXPECT_NE(table.find(Title(role)), std::string::npos) << Name(role);
    }
}

TEST(PermissionsMatrix, TitlesAreDistinctAndNotEmpty) {
    std::set<std::string_view> titles;
    for (const auto action : kEveryAction) {
        EXPECT_FALSE(Title(action).empty()) << Name(action);
        titles.insert(Title(action));
    }

    EXPECT_EQ(titles.size(), kEveryAction.size()) << "два действия названы одинаково";

    for (const auto threshold : kEveryAgeThreshold) {
        EXPECT_FALSE(Title(threshold).empty()) << Name(threshold);
    }
}

/// Матрица возраста собирается тем же опросом и говорит то, чего не видно в
/// матрице ролей: у одного и того же действия ответ меняется со временем.
TEST(PermissionsMatrix, TheAgeTableShowsTheHandover) {
    const testing::FakeFaults faults;
    const PolicySet permissions{faults};

    const auto table = RenderAgeMatrix(permissions, Thresholds());

    EXPECT_NE(table.find("младше 14"), std::string::npos);
    EXPECT_NE(table.find("с 18"), std::string::npos);
    EXPECT_NE(table.find("`pay_invoice`"), std::string::npos);
    EXPECT_EQ(table.find("`set_tariff`"), std::string::npos)
        << "в таблицу возраста попало действие, которого ни ученику, ни опекуну не дают";
}

TEST(PermissionsMatrix, ADocumentWithoutMarkersIsRefused) {
    const testing::FakeFaults faults;
    const PolicySet permissions{faults};

    const auto refused = WithMatrix("# Просто текст\n", permissions, Thresholds());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "permissions_matrix_markers_missing");
}

/// ГЛАВНЫЙ ТЕСТ МАТРИЦЫ: файл в docs совпадает с тем, что отвечают политики.
///
/// Матрица, написанная руками, расходится с кодом на первой правке и после
/// этого хуже, чем её отсутствие: по ней принимают решения, а она врёт. Поэтому
/// она не пишется, а собирается — опросом набора по всем действиям, ролям и
/// отношениям.
///
/// Обновить файл: `make permissions-lock`.
TEST(PermissionsMatrix, TheFileInDocsSaysWhatTheCodeDoes) {
    const testing::FakeFaults faults;
    const PolicySet permissions{faults};

    const auto document = Read(InDocs());
    ASSERT_FALSE(document.empty()) << "не читается " << InDocs();

    const auto fresh = WithMatrix(document, permissions, Thresholds());
    ASSERT_TRUE(fresh.HasValue()) << fresh.Failure().Detail();

    std::ofstream{InBuild()} << fresh.Value();

    EXPECT_EQ(document, fresh.Value())
        << "матрица прав в docs/architecture/permissions.md разошлась с политиками. "
           "Обновить: make permissions-lock";
}

}  // namespace
}  // namespace pdr::identity::policies
