#pragma once

#include <string>

#include "core/errors.hpp"

namespace pdr::infrastructure::http {

/// Спецификация, прочитанная с диска и переведённая в JSON один раз при старте.
///
/// ФАЙЛ НА ДИСКЕ ОДИН, и он источник правды: сервис не собирает спецификацию из
/// своего кода — тогда источником правды стала бы реализация, и «мы поменяли
/// ответ» перестало бы отличаться от «мы поменяли контракт». Здесь только смена
/// записи: человек читает YAML, инструменты порождения клиентов читают JSON.
///
/// Что перевод ничего не потерял, проверяется contract-набором: он сравнивает
/// выданный JSON с тем же файлом, разобранным настоящим разборщиком YAML. Без
/// этой сверки неоднозначный скаляр (`'true'`, `''`) уехал бы в другой тип
/// молча — и клиент получил бы схему, которой в спецификации нет.
class OpenApiDocument final {
public:
    static core::Result<OpenApiDocument> FromFile(const std::string& path);

    const std::string& Json() const noexcept {
        return json_;
    }

private:
    explicit OpenApiDocument(std::string json) noexcept : json_{std::move(json)} {}

    std::string json_;
};

}  // namespace pdr::infrastructure::http
