#pragma once

#include <string_view>

namespace pdr::testing {

/// Проверка для unit-тестов: печатает файл, строку и само условие, помечает
/// прогон неуспешным. Работает одинаково в Debug и Release — assert из
/// <cassert> в Release исчезает, и тест начинает проходить, ничего не проверив.
///
/// Это не фреймворк и не претендует на него: gtest со своими фикстурами и
/// параметризацией заводится отдельной задачей области TST. До тех пор
/// зависимостей у тестов нет вовсе, и запускаются они за миллисекунды.
void Check(bool condition, std::string_view expression, std::string_view file, int line);

/// Итог прогона: 0 — все проверки прошли, 1 — была хотя бы одна неуспешная.
int Summary(std::string_view suite);

}  // namespace pdr::testing

#define PDR_CHECK(condition) ::pdr::testing::Check((condition), #condition, __FILE__, __LINE__)
