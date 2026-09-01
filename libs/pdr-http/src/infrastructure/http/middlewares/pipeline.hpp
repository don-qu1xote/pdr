#pragma once

#include <string_view>

#include <userver/server/request/request_context.hpp>

#include "infrastructure/http/authorized_handler.hpp"

namespace pdr::infrastructure::http {

/// Имя, под которым звенья конвейера складывают сделанное. Одно на все звенья:
/// заготовка у запроса одна, и собирают её по частям.
inline constexpr std::string_view kPreparedName = "pdr-prepared";

/// Заготовка запроса в контексте: звено дописывает своё, ручка читает готовое.
///
/// Заводится первым же обратившимся звеном. Порядок звеньев задаётся
/// `PipelineBuilder` в статическом конфиге, и ни одно из них не обязано быть
/// первым: ручка, до которой конвейер не дошёл, получит пустую заготовку и
/// откажет разбором — а не тихо отработает на чужих данных.
Prepared& PreparedIn(userver::server::request::RequestContext& context);

/// То же для чтения. Заготовки нет — значит, конвейер до ручки не дошёл вовсе.
const Prepared& PreparedOf(userver::server::request::RequestContext& context);

}  // namespace pdr::infrastructure::http
