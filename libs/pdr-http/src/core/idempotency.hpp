#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::http {

/// Ключ идемпотентности — тот, что клиент присылает заголовком.
///
/// Значение, а не строка: ключ придумывает КЛИЕНТ, и это единственное место, где
/// чужая строка становится частью первичного ключа. Слишком короткий ключ —
/// «1», «a» — сталкивается с чужим в первый же день и превращает защиту от
/// повтора в отказ постороннему человеку; слишком длинный раздувает индекс.
/// Пределы те же, что в схеме, и это не совпадение: домен и база отвергают
/// одно и то же.
class IdempotencyKey final {
public:
    static constexpr std::size_t kShortest = 8;
    static constexpr std::size_t kLongest = 255;

    static core::Result<IdempotencyKey> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

    friend bool operator==(const IdempotencyKey&, const IdempotencyKey&) = default;

private:
    explicit IdempotencyKey(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

/// Отпечаток тела запроса: SHA-256 в шестнадцатеричной записи.
///
/// ТЕЛО ЦЕЛИКОМ НЕ ХРАНИТСЯ. В нём бывают персональные данные, и таблица
/// служебного механизма стала бы вторым местом, где они лежат, — со своим
/// сроком, своей выгрузкой и своим удалением. Вопрос к телу ровно один: «то же
/// самое или другое», и на него отвечает отпечаток.
///
/// Самого счёта здесь нет: SHA-256 живёт в userver, а домен собирается без
/// него. Считает `infrastructure/http/fingerprint.hpp`.
class RequestFingerprint final {
public:
    static core::Result<RequestFingerprint> Parse(std::string_view text);

    const std::string& Value() const noexcept {
        return value_;
    }

    friend bool operator==(const RequestFingerprint&, const RequestFingerprint&) = default;

private:
    explicit RequestFingerprint(std::string value) noexcept : value_{std::move(value)} {}

    std::string value_;
};

/// Метод обращения. Список закрыт: метода, которого здесь нет, для нас не
/// существует, и «а вдруг придёт что-то ещё» отвечает отказом, а не догадкой.
enum class Method : std::uint8_t {
    kGet,
    kHead,
    kOptions,
    kPost,
    kPut,
    kPatch,
    kDelete,

    kBoundary,
};

std::string_view Name(Method method) noexcept;

/// МЕНЯЕТ ЛИ ОБРАЩЕНИЕ СОСТОЯНИЕ. От этого зависит, обязателен ли ключ.
///
/// Ключ спрашивается со всех четырёх меняющих методов, включая DELETE: «удалить
/// занятие» повторяется по оборванной связи ровно так же, как «создать», и
/// второе удаление попадает уже в чужое занятие, занявшее тот же слот.
constexpr bool Mutating(Method method) noexcept {
    return method == Method::kPost || method == Method::kPut || method == Method::kPatch ||
           method == Method::kDelete;
}

/// Что делать с обращением, у которого ключ уже был.
enum class KeyState : std::uint8_t {
    /// Занят, операция идёт прямо сейчас — возможно, на другой реплике.
    kInProgress,

    /// Операция прошла, ответ сохранён.
    kCompleted,

    kBoundary,
};

std::string_view Name(KeyState state) noexcept;

std::optional<KeyState> ParseKeyState(std::string_view text);

/// Сохранённый ответ: ровно то, чем отвечали в первый раз.
struct SavedAnswer final {
    int status{0};
    std::string body;

    friend bool operator==(const SavedAnswer&, const SavedAnswer&) = default;
};

/// Чем кончилась попытка занять ключ.
///
/// Три исхода и ни одного больше:
///
///   kTaken       ключ наш, операцию надо выполнить;
///   kReplay      ключ занят и завершён — отдать сохранённое, НЕ выполнять;
///   kInFlight    ключ занят и операция идёт — отказать и попросить повторить.
///
/// Четвёртый случай — тот же ключ с ДРУГИМ телом — исходом не является: это
/// ошибка клиента, и она приходит отказом `idempotency_key_reused`.
enum class ClaimOutcome : std::uint8_t {
    kTaken,
    kReplay,
    kInFlight,

    kBoundary,
};

std::string_view Name(ClaimOutcome outcome) noexcept;

/// Результат попытки занять ключ вместе с тем, что отдавать при повторе.
struct Claim final {
    ClaimOutcome outcome{ClaimOutcome::kTaken};
    SavedAnswer answer;
};

/// Срок жизни ключа. Величина из динамического конфига, поэтому значение, а не
/// число: разбор с пределами живёт в одном месте, а не в каждом читателе.
class KeyLifetime final {
public:
    static constexpr int kShortestHours = 1;
    static constexpr int kLongestHours = 720;

    static core::Result<KeyLifetime> Compose(int hours);

    core::Instant::Duration Value() const noexcept {
        return value_;
    }

    core::Instant ExpiresFrom(core::Instant now) const noexcept {
        return now + value_;
    }

private:
    explicit KeyLifetime(core::Instant::Duration value) noexcept : value_{value} {}

    core::Instant::Duration value_;
};

}  // namespace pdr::http
