#pragma once

#include <optional>
#include <string_view>

#include "core/secret_string.hpp"

namespace pdr::application::ports {

/// Откуда берутся секреты. Узкий порт: один вопрос и ни одного больше.
///
/// Реализаций две, как у всякого выхода наружу: настоящая читает файл secdist
/// (`infrastructure::SecdistSecretSource`), проверочная — то, что ей положили
/// (`testing::FakeSecretSource`). Без второй проверка «сервис не поднимается без
/// секрета» требовала бы поднятого сервиса, то есть не существовала бы.
///
/// Возвращает `std::nullopt`, когда секрета НЕТ, и пустой секрет, когда он есть
/// и пуст. Разница не косметическая: «переменную забыли» и «переменную завели
/// пустой» — разные ошибки у того, кто ставит сервис, и сообщения им нужны
/// разные.
class SecretSource {
public:
    SecretSource(const SecretSource&) = delete;
    SecretSource& operator=(const SecretSource&) = delete;

    virtual ~SecretSource() = default;

    virtual std::optional<core::SecretString> Find(std::string_view name) const = 0;

protected:
    SecretSource() = default;
};

}  // namespace pdr::application::ports
