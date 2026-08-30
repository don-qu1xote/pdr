#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pdr::infrastructure::http {

/// Ответ об отказе по RFC 9457. ФОРМА ОДНА НА ВСЮ СИСТЕМУ.
///
/// Задаётся один раз затем, что иначе через полгода клиент разбирает пять
/// форматов: один хендлер отдаёт `{"error": "..."}`, другой — `{"message":
/// ...}`, третий просто текст. Разбирать их приходится всем, а чинить — никому.
///
/// `request_id` — расширение RFC, и оно здесь не для красоты: жалобу «у меня не
/// работает» разбирают по нему, а человек может назвать только то, что видел
/// сам. Значит, идентификатор обязан быть В ОТВЕТЕ, а не только в логах.
///
/// `field` тоже расширение и появляется только у отказа разбора: «bad request»
/// без имени поля отправляет человека угадывать, а разработчика — читать схему
/// глазами.
struct Problem final {
    /// Стабильный опознаватель рода отказа: `urn:pdr:error:<код>`.
    ///
    /// URN, а не ссылка: страницы с описанием у нас нет, а `type`, ведущий в
    /// никуда, — обещание, которого мы не выполняем. Имя домена в коде тоже не
    /// заводится: его пришлось бы менять вместе с площадкой.
    std::string type;

    /// Короткое имя рода отказа. Одно на код, не зависит от обстоятельств.
    std::string title;

    int status{0};

    /// Что именно случилось в этот раз. Для человека и для журнала; клиент
    /// разбирает `type` и код, а не эту строку.
    std::string detail;

    /// Путь запроса, на котором отказали.
    std::string instance;

    std::string request_id;

    /// Поле, из-за которого отказ, — путь внутри тела запроса.
    std::optional<std::string> field;

    friend bool operator==(const Problem&, const Problem&) = default;
};

/// Тип содержимого ответа об отказе. Не `application/json`: клиенту нужно уметь
/// отличить отказ от обычного ответа, не разбирая тело.
inline constexpr std::string_view kProblemContentType = "application/problem+json";

/// Приставка опознавателя. Код отказа приписывается к ней целиком.
inline constexpr std::string_view kProblemTypePrefix = "urn:pdr:error:";

std::string ProblemType(std::string_view code);

/// Тело ответа. ЕДИНСТВЕННОЕ МЕСТО, где отказ превращается в JSON.
std::string Render(const Problem& problem);

}  // namespace pdr::infrastructure::http
