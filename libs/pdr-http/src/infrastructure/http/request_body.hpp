#pragma once

#include <string>
#include <string_view>

#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/to.hpp>

#include "core/errors.hpp"

namespace pdr::infrastructure::http {

/// Имя поля из сообщения штатного разборщика.
///
/// Разбор сообщения — не изящно, и это осознанная плата. Штатный разборщик
/// сообщает путь двумя способами: часть отказов приходит `ExceptionWithPath`, у
/// которого путь можно спросить, а часть — обычным исключением, у которого путь
/// только в тексте («Error at path 'email': ...»). Второй случай — это ровно
/// проверки схемы: длина, образец, лишнее свойство. Отказаться от них значило бы
/// потерять имя поля там, где оно нужнее всего.
///
/// Пустая строка, когда пути в сообщении нет: тело, которое не разобралось как
/// JSON вовсе, ни на какое поле не указывает.
std::string FieldOfMessage(std::string_view message);

/// ТЕЛО ЗАПРОСА → ПОРОЖДЁННЫЙ ТИП. Единственное место, где это происходит.
///
/// Разбирает и проверяет ОДНО И ТО ЖЕ действие: у порождённого типа проверки
/// схемы — обязательные поля, длины, образцы, запрет лишних свойств — уже внутри
/// разборщика. Второй схемы рядом с ручкой не лежит, и разойтись с первой
/// нечему: обе порождены из `docs/api/openapi.yaml`.
///
/// ДВА РОДА ОТКАЗА, А НЕ ОДИН, и это видно клиенту: `request_not_json` — тело не
/// JSON вообще, `request_field_invalid` — JSON, но не тот. Слить их в один
/// значило бы отвечать «bad request» и на оборванную передачу, и на опечатку в
/// имени поля.
///
/// Путь до поля уходит в ответ отдельным членом: «bad request» без имени поля
/// отправляет человека угадывать, а разработчика — читать схему глазами.
template<class Body>
core::Result<Body> ParseBody(std::string_view text, std::string& field) {
    field.clear();

    userver::formats::json::Value json;
    try {
        json = userver::formats::json::FromString(text);
    } catch (const userver::formats::json::Exception& broken) {
        return core::Error{core::ErrorKind::kValidation, "request_not_json", broken.what()};
    }

    try {
        return Parse(json, userver::formats::parse::To<Body>{});
    } catch (const userver::formats::json::ExceptionWithPath& refused) {
        field = std::string{refused.GetPath()};
        return core::Error{core::ErrorKind::kValidation,
                           "request_field_invalid",
                           "поле «" + field + "» не проходит схему: " + refused.what()};
    } catch (const userver::formats::json::Exception& refused) {
        field = FieldOfMessage(refused.what());
        return core::Error{core::ErrorKind::kValidation,
                           "request_field_invalid",
                           "поле «" + field + "» не проходит схему: " + refused.what()};
    }
}

}  // namespace pdr::infrastructure::http
