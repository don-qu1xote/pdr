#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "identity/core/email.hpp"

namespace pdr::identity {

/// Что стало с одной строкой вставленного списка.
///
/// Приговор у каждой строки СВОЙ. Список из двадцати контактов, отвергнутый
/// целиком из-за одной опечатки, — это двадцать минут ручной сверки; список,
/// принятый целиком с молча пропущенной опечаткой, — один ученик, который так
/// и не пришёл, и никто не знает почему.
enum class ContactVerdict : std::uint8_t {
    /// Адрес разобран, такого приглашения ещё не было — позовём.
    kReady,
    /// На адрес не похоже. Строка показывается человеку как есть: угадывать за
    /// него, что он имел в виду, мы не беремся.
    kMalformed,
    /// Этот же адрес уже встречался выше в том же списке.
    kRepeatedInList,
    /// Действующее приглашение на этот адрес уже выдано. ВТОРОГО ПИСЬМА НЕ
    /// БУДЕТ: человек, получивший два приглашения от одного репетитора, решает,
    /// что первое не сработало, и открывает оба.
    kAlreadyInvited,
    /// Такой человек уже в практике — звать некуда.
    kAlreadyEnrolled,

    /// ГРАНИЦА СПИСКА, а не приговор.
    kBoundary,
};

std::string_view Name(ContactVerdict verdict) noexcept;

std::optional<ContactVerdict> ParseContactVerdict(std::string_view text);

inline constexpr std::array<ContactVerdict, 5> kEveryContactVerdict{
    ContactVerdict::kReady,
    ContactVerdict::kMalformed,
    ContactVerdict::kRepeatedInList,
    ContactVerdict::kAlreadyInvited,
    ContactVerdict::kAlreadyEnrolled,
};

static_assert(kEveryContactVerdict.size() == static_cast<std::size_t>(ContactVerdict::kBoundary),
              "приговор заведён, а в kEveryContactVerdict его нет");

/// Одна строка списка: что вставили, что разобрали и что с этим будет.
class Contact final {
public:
    Contact(std::string raw, std::optional<Email> mail, ContactVerdict verdict) noexcept
        : raw_{std::move(raw)}, mail_{std::move(mail)}, verdict_{verdict} {}

    /// Строка как её вставил человек. Показывается в предпросмотре рядом с
    /// приговором: «строка 7 не разобралась» человеку ничего не говорит.
    const std::string& Raw() const noexcept {
        return raw_;
    }
    const std::optional<Email>& Mail() const noexcept {
        return mail_;
    }
    ContactVerdict Verdict() const noexcept {
        return verdict_;
    }

    Contact Judged(ContactVerdict verdict) const {
        return Contact{raw_, mail_, verdict};
    }

    friend bool operator==(const Contact&, const Contact&) = default;

private:
    std::string raw_;
    std::optional<Email> mail_;
    ContactVerdict verdict_;
};

/// Список контактов, вставленный целиком.
///
/// У переезжающего репетитора двадцать учеников, и по одному он их звать не
/// станет — он скопирует их из своей таблицы или из переписки. Поэтому здесь
/// разбирается то, что реально вставляют: адреса через запятую, через точку с
/// запятой, по одному на строке и в виде «Имя Фамилия <адрес>».
///
/// РАЗБОР НИЧЕГО НЕ ЗНАЕТ О ПРАКТИКЕ. Он не ходит в базу и не решает, кого уже
/// звали: это чистая функция от текста, и проверяется она таблицей. Приговоры
/// «уже позвали» и «уже здесь» ставит сценарий предпросмотра, у которого есть
/// хранилище.
class ContactList final {
public:
    /// Ограничение сверху — не вкус, а защита от вставки чужой базы целиком.
    /// Двадцать учеников в списке обычны, двадцать тысяч — нет.
    static constexpr std::size_t kMostLines = 500;

    static ContactList Parse(std::string_view pasted);

    static ContactList Of(std::vector<Contact> lines) noexcept {
        return ContactList{std::move(lines)};
    }

    const std::vector<Contact>& Lines() const noexcept {
        return lines_;
    }

    /// Сколько строк сейчас годны к отправке.
    std::size_t Ready() const noexcept;

    bool Empty() const noexcept {
        return lines_.empty();
    }

private:
    explicit ContactList(std::vector<Contact> lines) noexcept : lines_{std::move(lines)} {}

    std::vector<Contact> lines_;
};

}  // namespace pdr::identity
