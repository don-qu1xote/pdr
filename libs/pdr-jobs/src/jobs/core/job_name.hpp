#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pdr::jobs {

/// Имя задания. Оно же — имя строки блокировки в базе и значение метки в
/// метрике, поэтому форма проверяется здесь, а не «в конфиге же виднее».
///
/// Имя приходит из динамического конфига (`PDR-CFG-01`) и не бывает константой в
/// коде: переименовать блокировку без выкатки — это не удобство, а способ
/// остановить задание, которое сошло с ума, не останавливая процесс.
///
/// Строчные буквы, цифры и разделители `.`, `-`, `_`; начинается буквой,
/// заканчивается буквой или цифрой; не длиннее шестидесяти четырёх знаков.
/// Пробел в имени блокировки — это две разные блокировки, отличающиеся
/// невидимым символом, и два воркера, каждый уверенный, что он один.
class JobName final {
public:
    static constexpr std::size_t kMaxLength = 64;

    static std::optional<JobName> Parse(std::string_view name);

    const std::string& Value() const noexcept {
        return name_;
    }

    friend bool operator==(const JobName&, const JobName&) = default;

private:
    explicit JobName(std::string name) noexcept : name_{std::move(name)} {}

    std::string name_;
};

}  // namespace pdr::jobs
