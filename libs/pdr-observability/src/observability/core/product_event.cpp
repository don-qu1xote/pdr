#include "observability/core/product_event.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace pdr::observability {
namespace {

constexpr int kFirstVersion = 1;

constexpr std::array<std::string_view, 6> kPersonWords{
    "person",
    "email",
    "phone",
    "login",
    "name",
    "passport",
};

bool IsLower(char symbol) noexcept {
    return symbol >= 'a' && symbol <= 'z';
}

bool IsDigit(char symbol) noexcept {
    return symbol >= '0' && symbol <= '9';
}

bool IsNameSymbol(char symbol) noexcept {
    return IsLower(symbol) || IsDigit(symbol) || symbol == '_';
}

/// Часть имени: начинается буквой, дальше строчные, цифры и подчёркивание.
bool IsNamePart(std::string_view part) noexcept {
    if (part.empty() || !IsLower(part.front())) {
        return false;
    }
    for (const char symbol : part) {
        if (!IsNameSymbol(symbol)) {
            return false;
        }
    }
    return true;
}

/// Имя типа — «контекст.что_произошло». По нему видно издателя, и подписчик не
/// гадает, чьё это событие.
bool IsEventType(std::string_view type) noexcept {
    const auto dot = type.find('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    return IsNamePart(type.substr(0, dot)) && IsNamePart(type.substr(dot + 1));
}

bool Contains(std::string_view name, std::string_view word) noexcept {
    return name.find(word) != std::string_view::npos;
}

core::Error Refuse(std::string code, std::string detail) {
    return core::Error{core::ErrorKind::kValidation, std::move(code), std::move(detail)};
}

}  // namespace

std::string_view Name(Role role) noexcept {
    switch (role) {
        case Role::kTutor:
            return "tutor";
        case Role::kStudent:
            return "student";
        case Role::kGuardian:
            return "guardian";
        case Role::kSystem:
            return "system";
    }
    return "system";
}

std::string_view Name(ValueKind kind) noexcept {
    switch (kind) {
        case ValueKind::kCount:
            return "count";
        case ValueKind::kMinutes:
            return "minutes";
        case ValueKind::kHours:
            return "hours";
        case ValueKind::kDays:
            return "days";
        case ValueKind::kFlag:
            return "flag";
        case ValueKind::kBucket:
            return "bucket";
        case ValueKind::kCode:
            return "code";
        case ValueKind::kReference:
            return "reference";
        case ValueKind::kScore:
            return "score";
    }
    return "code";
}

Value Value::Count(std::int64_t count) noexcept {
    return Value{ValueKind::kCount, count};
}

Value Value::Minutes(std::int64_t minutes) noexcept {
    return Value{ValueKind::kMinutes, minutes};
}

Value Value::Hours(std::int64_t hours) noexcept {
    return Value{ValueKind::kHours, hours};
}

Value Value::Days(std::int64_t days) noexcept {
    return Value{ValueKind::kDays, days};
}

Value Value::Flag(bool yes) noexcept {
    return Value{ValueKind::kFlag, static_cast<std::int64_t>(yes ? 1 : 0)};
}

Value Value::Bucket(std::string bucket) {
    return Value{ValueKind::kBucket, std::move(bucket)};
}

Value Value::Code(std::string code) {
    return Value{ValueKind::kCode, std::move(code)};
}

Value Value::Score(std::int64_t score) noexcept {
    return Value{ValueKind::kScore, score};
}

std::int64_t Value::Number() const {
    if (kind_ == ValueKind::kFlag || kind_ == ValueKind::kBucket || kind_ == ValueKind::kCode ||
        kind_ == ValueKind::kReference) {
        throw std::logic_error{"Value::Number() у значения вида " + std::string{Name(kind_)}};
    }
    return number_;
}

bool Value::Yes() const {
    if (kind_ != ValueKind::kFlag) {
        throw std::logic_error{"Value::Yes() у значения вида " + std::string{Name(kind_)}};
    }
    return number_ != 0;
}

std::optional<core::Error> AnonymityBreach(const Fields& fields) {
    for (const auto& [name, value] : fields) {
        if (name == "id" || (name.size() > 3 && name.ends_with("_id"))) {
            return Refuse("person_in_product_event",
                          "поле «" + name +
                              "» — идентификатор. В продуктовом событии его нет: ссылка "
                              "только на арендатора и роль");
        }
        for (const std::string_view word : kPersonWords) {
            if (Contains(name, word)) {
                return Refuse("person_in_product_event",
                              "поле «" + name + "» именует человека («" + std::string{word} +
                                  "»). Событие обезличено на уровне записи");
            }
        }

        const bool is_text = value.Kind() == ValueKind::kCode || value.Kind() == ValueKind::kBucket;
        if (is_text && core::detail::ParseUuid(value.Text()).has_value()) {
            return Refuse("person_in_product_event",
                          "поле «" + name +
                              "» несёт идентификатор под видом кода. Код — это перечень, а "
                              "не ссылка; ссылкой бывает Reference, и человеком она не "
                              "бывает по устройству типа");
        }
    }
    return std::nullopt;
}

core::Result<ProductEvent> ProductEvent::Compose(core::TenantId tenant,
                                                 std::string type,
                                                 int version,
                                                 Role actor,
                                                 core::Instant occurred_at,
                                                 Fields fields) {
    if (!IsEventType(type)) {
        return Refuse("event_type_malformed",
                      "«" + type +
                          "» не похоже на имя события. Имя — «контекст.что_произошло»: по "
                          "нему видно издателя");
    }
    if (version < kFirstVersion) {
        return Refuse("event_version_below_one",
                      "версия схемы меньше единицы. Схема без версии запрещена: менять её "
                      "придётся, и не один раз");
    }
    if (fields.empty()) {
        return Refuse("event_without_fields",
                      "событие без полей ничего не измеряет. Реестр вопросов называет, что "
                      "именно нужно посчитать: docs/product/open-questions.md");
    }
    if (const auto breach = AnonymityBreach(fields); breach.has_value()) {
        return *breach;
    }

    return ProductEvent{
        std::move(tenant), std::move(type), version, actor, occurred_at, std::move(fields)};
}

ProductEvent::ProductEvent(core::TenantId tenant,
                           std::string type,
                           int version,
                           Role actor,
                           core::Instant occurred_at,
                           Fields fields)
    : tenant_{tenant},
      type_{std::move(type)},
      version_{version},
      actor_{actor},
      occurred_at_{occurred_at},
      fields_{std::move(fields)} {}

std::optional<Value> ProductEvent::Field(std::string_view name) const {
    const auto found = fields_.find(name);
    if (found == fields_.end()) {
        return std::nullopt;
    }
    return found->second;
}

}  // namespace pdr::observability
