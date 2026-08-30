#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pdr::identity {

/// Категория персональных данных из перечня (docs/legal/personal-data.md).
///
/// ПЕРЕЧЕНЬ СТОИТ ВЫШЕ КОДА, А НЕ РЯДОМ. Он определяет, что мы вообще имеем
/// право отправить во внешнюю модель; написанный после кода, он либо запретил
/// бы уже сделанное, либо оказался бы враньём. Поэтому здесь не «то, что уже
/// хранится», а весь перечень целиком — половины этих данных в дереве ещё нет.
///
/// Список закрытый: категория, заведённая в коде и не описанная в перечне, —
/// это данные, которые мы собираем, не сказав человеку. Сверяет
/// `scripts/check_personal_data.py` в обе стороны.
enum class PersonalDataCategory : std::uint8_t {
    /// Имя, почта, часовой пояс, отпечаток пароля.
    kAccount,

    /// Имя ребёнка, дата рождения, кто его опекун.
    kChildAndGuardian,

    /// Когда занятие, состоялось ли, кто перенёс.
    kScheduleAndAttendance,

    /// Сумма, дата, за что платили, номер чека.
    kPaymentsAndReceipts,

    /// Сообщения между учеником и репетитором.
    kMessages,

    /// Видео и звук занятия, текст сказанного.
    kRecordingsAndTranscripts,

    /// Что решено, что получается, что стоит повторить.
    kLearningResults,

    /// Какие страницы открывали, с какого устройства, когда.
    kTechnicalRecords,

    /// ГРАНИЦА СПИСКА, а не категория.
    kBoundary,
};

/// Машинный код категории: то же слово, что в перечне.
std::string_view Name(PersonalDataCategory category) noexcept;

std::optional<PersonalDataCategory> ParsePersonalDataCategory(std::string_view text);

inline constexpr std::array<PersonalDataCategory, 8> kEveryPersonalDataCategory{
    PersonalDataCategory::kAccount,
    PersonalDataCategory::kChildAndGuardian,
    PersonalDataCategory::kScheduleAndAttendance,
    PersonalDataCategory::kPaymentsAndReceipts,
    PersonalDataCategory::kMessages,
    PersonalDataCategory::kRecordingsAndTranscripts,
    PersonalDataCategory::kLearningResults,
    PersonalDataCategory::kTechnicalRecords,
};

static_assert(kEveryPersonalDataCategory.size() ==
                  static_cast<std::size_t>(PersonalDataCategory::kBoundary),
              "категория заведена, а в kEveryPersonalDataCategory её нет: экран «мои данные» "
              "покажет не всё, что мы храним");

/// Кому данные уходят наружу.
///
/// Тот же список, что в ревизии открытости (docs/architecture/openness.md), и
/// расхождение роняет сборку. Получатель, которого нет в ревизии, — это
/// зависимость, о которой мы никому не сказали.
enum class Recipient : std::uint8_t {
    kPaymentProvider,
    kReceiptService,
    kModelProvider,
    kHandwriting,
    kHosting,

    kBoundary,
};

std::string_view Name(Recipient recipient) noexcept;

std::optional<Recipient> ParseRecipient(std::string_view text);

inline constexpr std::array<Recipient, 5> kEveryRecipient{
    Recipient::kPaymentProvider,
    Recipient::kReceiptService,
    Recipient::kModelProvider,
    Recipient::kHandwriting,
    Recipient::kHosting,
};

static_assert(kEveryRecipient.size() == static_cast<std::size_t>(Recipient::kBoundary),
              "получатель заведён, а в kEveryRecipient его нет: экран «мои данные» умолчит "
              "о том, кому ушли данные");

}  // namespace pdr::identity
