#pragma once

#include <concepts>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::observability {

/// Роль, от лица которой произошло действие. Это ВСЁ, что продуктовое событие
/// знает о человеке: список закрыт, и четвёртая роль — «система» — нужна затем,
/// чтобы отмена по расписанию не выглядела отменой репетитора.
enum class Role : std::uint8_t {
    kTutor,
    kStudent,
    kGuardian,
    kSystem,
};

/// Код роли: то же слово, что в ограничении observability_product_event_role_known.
std::string_view Name(Role role) noexcept;

/// Вид значения поля. Тот же закрытый список, что в configs/product-events.yaml,
/// и закрыт он затем, чтобы «свободный текст» не появился в обезличенном потоке
/// как ни в чём не бывало: любая фраза, написанная человеком, рано или поздно
/// оказывается про конкретную семью.
enum class ValueKind : std::uint8_t {
    kCount,
    kMinutes,
    kHours,
    kDays,
    kFlag,
    kBucket,
    kCode,
    kReference,
    kScore,
};

std::string_view Name(ValueKind kind) noexcept;

/// Значение поля продуктового события.
///
/// ССЫЛКУ НА ЧЕЛОВЕКА В НЕГО НЕ ПОЛОЖИТЬ: `Reference` не принимает `PersonId`, и
/// это не соглашение, а ошибка компиляции — цель
/// `pdr_compile_fail_person_in_product_event` обязана не собираться. Остальные
/// сущности ссылкой быть могут: материал и умение — не люди.
///
/// Отсюда правило, которому подчиняются все поля реестра: СЧИТАЕТ ИЗДАТЕЛЬ, А НЕ
/// АНАЛИТИК. «Какое это занятие по счёту у ученика» вычисляет тот, кто пишет
/// событие и знает ученика; в событие попадает число. Иначе понадобился бы ключ,
/// связывающий записи одного человека между собой, то есть тот самый
/// идентификатор — только окольным путём.
class Value final {
public:
    static Value Count(std::int64_t count) noexcept;
    static Value Minutes(std::int64_t minutes) noexcept;
    static Value Hours(std::int64_t hours) noexcept;
    static Value Days(std::int64_t days) noexcept;
    static Value Flag(bool yes) noexcept;
    static Value Bucket(std::string bucket);
    static Value Code(std::string code);
    static Value Score(std::int64_t score) noexcept;

    /// Ссылка на сущность, которая НЕ человек: материал, умение, задание.
    template<class Tag>
        requires(!std::same_as<Tag, core::PersonTag>)
    static Value Reference(const core::StrongId<Tag>& id) {
        return Value{ValueKind::kReference, id.ToString()};
    }

    ValueKind Kind() const noexcept {
        return kind_;
    }

    /// Число у числового вида. Спросить число у флага — ошибка программиста, а
    /// не отказ домена, поэтому здесь исключение.
    std::int64_t Number() const;
    bool Yes() const;

    const std::string& Text() const noexcept {
        return text_;
    }

    friend bool operator==(const Value&, const Value&) = default;

private:
    Value(ValueKind kind, std::int64_t number) noexcept : kind_{kind}, number_{number} {}
    Value(ValueKind kind, std::string text) : kind_{kind}, text_{std::move(text)} {}

    ValueKind kind_;
    std::int64_t number_{0};
    std::string text_;
};

/// Поля события: имя — значение. Порядок не важен, поэтому упорядоченное
/// отображение: тогда две одинаковые записи и выглядят одинаково.
using Fields = std::map<std::string, Value, std::less<>>;

/// Публичный контракт контекста observability — ЕДИНСТВЕННЫЙ его заголовок,
/// который другим контекстам разрешено включать. Всё остальное в модуле для них
/// не существует: каталог src в чужую сборку не попадает, а попытку включить
/// внутренность ловит scripts/check_layers.py.
///
/// Контракт односторонний: издатель кладёт событие и не получает в ответ
/// ничего, кроме отказа. Отказ означает ошибку в самом событии — поле, именующее
/// человека, пустой набор полей, схема без версии, — и поэтому его нельзя не
/// заметить: результат помечен `[[nodiscard]]`.
class Contract {
public:
    Contract(const Contract&) = delete;
    Contract& operator=(const Contract&) = delete;

    virtual ~Contract() = default;

    [[nodiscard]] virtual core::Result<void> Record(const core::TenantId& tenant,
                                                    std::string_view type,
                                                    int version,
                                                    Role actor,
                                                    core::Instant occurred_at,
                                                    Fields fields) = 0;

protected:
    Contract() = default;
};

}  // namespace pdr::observability
