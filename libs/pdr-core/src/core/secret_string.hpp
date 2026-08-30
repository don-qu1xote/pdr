#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace pdr::core {

/// Секрет — значение, которое нельзя напечатать.
///
/// ЗАЩИТА ТИПОМ, А НЕ ДИСЦИПЛИНОЙ. «Мы не логируем секреты» — обещание, и
/// нарушается оно не злым умыслом, а строкой `LOG_INFO() << settings` при
/// разборе чужой задачи в пятницу. Здесь такая строка не собирается.
///
/// Вывод удалён ШАБЛОНОМ, а не одной перегрузкой для `std::ostream`. Журнал
/// userver — не `std::ostream`: `LOG_INFO()` возвращает свой приёмник со своим
/// `operator<<`, и перегрузка для потока стандартной библиотеки его не
/// касается. Шаблон закрывает любой приёмник, включая тот, которого ещё нет.
///
/// Достать значение можно ровно одним способом — `Reveal()`. Имя выбрано
/// длинным и заметным: оно должно бросаться в глаза на ревью там, где секрет
/// действительно уходит наружу — в строку подключения, в заголовок запроса, — и
/// нигде больше.
class SecretString final {
public:
    SecretString() noexcept = default;

    explicit SecretString(std::string value) noexcept : value_{std::move(value)} {}

    /// Единственный выход. Всё, что зовёт его, обязано объяснять зачем.
    const std::string& Reveal() const noexcept {
        return value_;
    }

    bool Empty() const noexcept {
        return value_.empty();
    }

    std::size_t Size() const noexcept {
        return value_.size();
    }

    /// Сравнение нужно ровно одному правилу — «секреты разного назначения не
    /// совпадают» (`application::VerifySecrets`). Постоянного времени здесь
    /// нет намеренно: сравниваются два НАШИХ значения при старте, а не
    /// присланное с чужим, и подбирать по времени тут нечего и некому.
    friend bool operator==(const SecretString&, const SecretString&) = default;

private:
    std::string value_;
};

/// Секрет не выводится НИКУДА: ни в поток, ни в журнал, ни в дамп конфигурации.
///
/// Отрицательная проверка `unit.compile_fail.secret_string_to_log` стережёт это:
/// цель, которая печатает секрет, обязана не собираться.
template<class Sink>
Sink&& operator<<(Sink&& sink, const SecretString& secret) = delete;

/// Строка-заменитель для мест, где о секрете надо СКАЗАТЬ, не показав его:
/// дамп настроек при старте, сообщение об ошибке, отчёт о проверке.
inline constexpr std::string_view kSecretMask = "«скрыто»";

}  // namespace pdr::core
