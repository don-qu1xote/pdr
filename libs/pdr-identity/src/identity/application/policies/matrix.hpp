#pragma once

#include <string>
#include <string_view>

#include "core/errors.hpp"
#include "identity/application/policies/policy_set.hpp"

namespace pdr::identity::policies {

/// Название действия для человека. То, что стоит в матрице прав.
std::string_view Title(Action action) noexcept;

/// Название роли для человека.
std::string_view Title(Role role) noexcept;

/// Матрица прав, СОБРАННАЯ ИЗ САМИХ ПОЛИТИК.
///
/// Не описание того, как задумано, а протокол опроса: каждое действие
/// спрашивается у набора за каждую роль и за каждое отношение к ресурсу, и в
/// клетку попадает то, что политика ответила. Матрица, написанная руками,
/// расходится с кодом на первой же правке и после этого хуже, чем её
/// отсутствие: по ней принимают решения, а она врёт.
std::string RenderMatrix(const PolicySet& permissions);

/// Метки области, которую занимает матрица в документе. Всё, что вне их, —
/// человеческий текст, и он пишется руками.
inline constexpr std::string_view kMatrixOpening = "<!-- матрица прав: начало -->";
inline constexpr std::string_view kMatrixClosing = "<!-- матрица прав: конец -->";

/// Документ с заново собранной матрицей внутри меток.
///
/// Чистая работа со строкой: читает и пишет файл тот, кому это положено слоем,
/// а сравнивают документы простым равенством — тогда «сверить» и «перезаписать»
/// пользуются одной и той же сборкой и разойтись не могут.
core::Result<std::string> WithMatrix(std::string_view document, const PolicySet& permissions);

}  // namespace pdr::identity::policies
