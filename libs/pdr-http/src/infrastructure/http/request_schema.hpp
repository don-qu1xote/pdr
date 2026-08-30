#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <userver/formats/json/schema.hpp>
#include <userver/formats/json/value.hpp>

#include "core/errors.hpp"

namespace pdr::infrastructure::http {

/// Схема тела запроса — рядом с хендлером, файлом, а не кодом.
///
/// Схемой, а не разбором руками: разбор руками отвечает «bad request», и
/// человек с разработчиком вдвоём гадают, какое поле не понравилось. Схема
/// называет путь до поля, и он уходит в ответ.
///
/// Проверяет штатный `formats::json::Schema` (ADR-0013): своего обхода схемы в
/// дереве нет и не будет.
///
/// Схема читается ОДИН РАЗ при сборке хендлера и отказывает сразу, если файла
/// нет или он не схема. Отложить это до первого запроса значило бы узнать о
/// сломанной схеме от первого человека, который на неё наткнулся.
///
/// Разобранная схема неизменяема, поэтому она разделяемая: копия хендлера не
/// перечитывает файл и не держит вторую копию схемы в памяти.
class RequestSchema final {
public:
    static core::Result<RequestSchema> FromText(std::string_view text);
    static core::Result<RequestSchema> FromFile(const std::string& path);

    /// Разобрать тело и проверить его схемой. Путь до поля, на котором разбор
    /// встал, кладётся в `field`: он уходит в ответ отдельным членом, потому
    /// что «bad request» без имени поля — это отказ без содержания.
    core::Result<userver::formats::json::Value> Parse(std::string_view body,
                                                      std::string& field) const;

private:
    explicit RequestSchema(std::shared_ptr<const userver::formats::json::Schema> schema) noexcept;

    std::shared_ptr<const userver::formats::json::Schema> schema_;
};

}  // namespace pdr::infrastructure::http
