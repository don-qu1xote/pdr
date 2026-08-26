#include "identity/core/password.hpp"

namespace pdr::identity {
namespace {

/// Нижние пределы Argon2id из рекомендации OWASP на 2024 год: 19 МиБ памяти,
/// два прохода, одна нить. Ниже этого счёт становится дешёвым для перебора,
/// поэтому предел стоит в домене, а не только в схеме реестра: конфиг правят
/// не только те, кто читал рекомендацию.
constexpr std::uint32_t kLeastMemoryKib = 19456;
constexpr std::uint32_t kLeastIterations = 2;
constexpr std::uint32_t kLeastParallelism = 1;

/// Argon2 требует, чтобы памяти было не меньше восьми блоков на нить.
constexpr std::uint32_t kBlocksPerLane = 8;

/// Ниже восьми знаков пароль перебирается целиком быстрее, чем человек
/// дочитывает это предложение.
constexpr std::size_t kLeastMinLength = 8;

constexpr std::string_view kArgon2idPrefix = "$argon2id$";

/// Длина в ЗНАКАХ, а не в байтах.
///
/// Пароль «пароль1234» — десять знаков и восемнадцать байт: считая байты, мы бы
/// требовали от русского пароля вдвое меньше знаков, чем от английского, и
/// никто бы этого не заметил. Стойкость меряется знаками — их и считаем.
///
/// Верхняя граница остаётся в байтах: она про работу, которую придётся
/// проделать Argon2, а работа зависит от байтов.
std::size_t Symbols(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const char symbol : text) {
        if ((static_cast<unsigned char>(symbol) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

}  // namespace

core::Result<PasswordRules> PasswordRules::Compose(std::uint32_t memory_kib,
                                                   std::uint32_t iterations,
                                                   std::uint32_t parallelism,
                                                   std::size_t min_length) {
    if (memory_kib < kLeastMemoryKib) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_memory_too_small",
                           "памяти на счёт хеша меньше " + std::to_string(kLeastMemoryKib) +
                               " КиБ: перебор становится дешёвым"};
    }
    if (iterations < kLeastIterations) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_iterations_too_few",
                           "проходов меньше " + std::to_string(kLeastIterations)};
    }
    if (parallelism < kLeastParallelism) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_parallelism_too_low",
                           "нитей счёта меньше одной"};
    }
    if (memory_kib < parallelism * kBlocksPerLane) {
        return core::Error{
            core::ErrorKind::kValidation,
            "password_memory_below_parallelism",
            "на каждую нить нужно не меньше " + std::to_string(kBlocksPerLane) + " КиБ памяти"};
    }
    if (min_length < kLeastMinLength) {
        return core::Error{
            core::ErrorKind::kValidation,
            "password_min_length_too_low",
            "меньше " + std::to_string(kLeastMinLength) + " знаков пароль перебирается целиком"};
    }
    if (min_length > Password::kMaxLength) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_min_length_above_max",
                           "порог длины выше того, что вообще принимается"};
    }

    return PasswordRules{memory_kib, iterations, parallelism, min_length};
}

core::Result<Password> Password::Chosen(std::string_view text, const PasswordRules& rules) {
    if (Symbols(text) < rules.MinLength()) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_too_short",
                           "нужно не меньше " + std::to_string(rules.MinLength()) + " знаков"};
    }

    return Given(text);
}

core::Result<Password> Password::Given(std::string_view text) {
    if (text.size() > kMaxLength) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_too_long",
                           "не больше " + std::to_string(kMaxLength) + " знаков"};
    }

    return Password{std::string{text}};
}

core::Result<PasswordHash> PasswordHash::Parse(std::string_view text) {
    if (text.rfind(kArgon2idPrefix, 0) != 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_hash_not_argon2id",
                           "хеш пароля записывается Argon2id и никак иначе"};
    }

    return PasswordHash{std::string{text}};
}

}  // namespace pdr::identity
