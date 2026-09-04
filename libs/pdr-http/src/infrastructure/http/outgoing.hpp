#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <userver/clients/http/client.hpp>
#include <userver/clients/http/request.hpp>
#include <userver/clients/http/response.hpp>
#include <userver/utils/function_ref.hpp>
#include <userver/utils/retry_budget.hpp>
#include <userver/utils/token_bucket.hpp>

namespace pdr::infrastructure::http {

/// Можно ли повторить это обращение.
///
/// Повтор неидемпотентного запроса — это второй платёж, а не вторая попытка
/// (PDR-API-02). Решает вызывающий, и решение видно в месте вызова.
enum class Repeatable : std::uint8_t { kNo, kYes };

/// ОДНО НАПРАВЛЕНИЕ НАРУЖУ: свой срок, свой бюджет повторов, своя квота.
///
/// Здесь не написано ни одного механизма: клиент, откат между попытками, бюджет
/// повторов и ведро токенов — штатные (ADR-0013). Написана только ПОЛИТИКА:
/// какой срок у этого направления, разрешает ли бюджет повтор и не превышена ли
/// квота провайдера. Всё остальное — вызов штатного.
///
/// Отсутствие ответа — не исключение, а `std::nullopt`. Чужой сервис по ADR-0014
/// украшение: «его сейчас нет» — обычный ход событий, и вызывающий обязан его
/// обработать, а не поймать.
class Outgoing final {
public:
    /// Как собрать обращение. Метод, адрес, заголовки и тело — дело вызывающего:
    /// направление ничего о них не знает и знать не должно.
    using Build = userver::utils::function_ref<userver::clients::http::Request(
        userver::clients::http::Client&)>;
    using Answer = std::shared_ptr<userver::clients::http::Response>;

    Outgoing(std::string direction,
             userver::clients::http::Client& client,
             std::chrono::milliseconds timeout,
             int attempts,
             userver::utils::RetryBudget& budget,
             userver::utils::TokenBucket& quota) noexcept;

    std::chrono::milliseconds Timeout() const noexcept {
        return timeout_;
    }

    /// Сходить наружу. Пусто — сервиса сейчас нет: не ответил, ответил
    /// пятисоткой, или мы сами превысили квоту провайдера.
    std::optional<Answer> Send(Repeatable repeatable, Build build) const;

private:
    std::string direction_;
    userver::clients::http::Client& client_;
    std::chrono::milliseconds timeout_;
    int attempts_;
    userver::utils::RetryBudget& budget_;
    userver::utils::TokenBucket& quota_;
};

}  // namespace pdr::infrastructure::http
