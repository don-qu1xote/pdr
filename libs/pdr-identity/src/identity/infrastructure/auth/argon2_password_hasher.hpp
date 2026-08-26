#pragma once

#include <cstddef>

#include "application/ports/secret_generator.hpp"
#include "identity/application/ports/password_hasher.hpp"

namespace pdr::identity {

/// Argon2id библиотекой libargon2 — эталонной реализацией конкурса паролей.
///
/// Своей схемы здесь нет и быть не может: «SHA с солью», «два раза SHA», «SHA с
/// перцем» подбираются на видеокарте со скоростью миллиардов в секунду, и это
/// не мнение, а измерение. Argon2id стоит памяти, а память на видеокарте не
/// умножается.
///
/// Соль берётся у `SecretGenerator`, а не у `std::rand`: предсказуемая соль
/// возвращает радужные таблицы, ради отказа от которых соль и существует. В
/// счёт она уезжает текстом base64url — это те же случайные байты, только в
/// записи, которая переживает любой перенос: соли нужна неповторимость, а не
/// конкретный алфавит.
///
/// Сравнение постоянное по времени — внутри самой библиотеки. Своего сравнения
/// строк здесь нет намеренно: разница в микросекунды между «первый байт не тот»
/// и «последний байт не тот» измеряется по сети.
///
/// СЧЁТ БЛОКИРУЕТ ПОТОК на десятки миллисекунд — так и задумано, в этом весь
/// смысл. Значит, в сервисе он обязан идти на отдельном task_processor, иначе
/// один вход тормозит все сопрограммы этого потока. Сервиса в дереве пока нет;
/// долг записан в docs/architecture/first-service.md.
class Argon2PasswordHasher final : public ports::PasswordHasher {
public:
    /// Шестнадцать байт соли и тридцать два байта хеша — то, что рекомендует
    /// сама спецификация Argon2.
    static constexpr std::size_t kSaltBytes = 16;
    static constexpr std::size_t kHashBytes = 32;

    explicit Argon2PasswordHasher(const application::ports::SecretGenerator& secrets) noexcept;

    core::Result<PasswordHash> Hash(const Password& password,
                                    const PasswordRules& rules) const override;

    bool Matches(const Password& password, const PasswordHash& hash) const override;

private:
    const application::ports::SecretGenerator& secrets_;
};

}  // namespace pdr::identity
