#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pdr::core {

/// Для чего секрет. Список закрытый: назначение — это то, что отличает один
/// секрет от другого, а «прочее» отличает его ни от чего.
enum class SecretPurpose : std::uint8_t {
    /// Подключение к единственному хранилищу.
    kDatabase,

    /// Обращения к платёжному провайдеру от нашего имени.
    kPaymentProvider,

    /// Проверка подписи входящих уведомлений провайдера. ОТДЕЛЬНОЕ назначение,
    /// а не то же, что обращения: ключ, которым мы ходим к провайдеру, и ключ,
    /// которым провайдер подписывает то, что шлёт нам, — разные ключи с разной
    /// судьбой. Совпали — значит, кто угодно, получивший один, подделает другое.
    kWebhookSigning,

    /// Доступ к серверу видеозанятий.
    kVideoProvider,

    kBoundary,
};

std::string_view Name(SecretPurpose purpose) noexcept;

/// Запись реестра секретов: имя, назначение и область задачи, которая заведёт
/// его читателя.
///
/// `awaits` пустое — секрет обязателен при старте ПРЯМО СЕЙЧАС. Непустое —
/// секрет решён, но читать его пока некому: спрашивать его у того, кто ставит
/// сервис сегодня, значит требовать ключ от двери, которой нет. Тот же приём,
/// что у реестра динамических значений (`configs/dynamic/registry.yaml`), и по
/// той же причине: решение попадает в реестр раньше, чем в код, и не остаётся
/// там навсегда — `scripts/check_secrets_registry.py` сверяет одно с другим в
/// обе стороны.
struct SecretSpec final {
    std::string_view name;
    SecretPurpose purpose;
    std::string_view awaits;

    bool RequiredNow() const noexcept {
        return awaits.empty();
    }
};

/// РЕЕСТР СЕКРЕТОВ. Тот же список, что в configs/secrets_registry.yaml, и
/// расхождение роняет сборку.
///
/// Значений по умолчанию здесь нет и быть не может: умолчание вида «change-me»
/// опаснее отсутствия, потому что доживает до прода и работает.
inline constexpr std::array<SecretSpec, 6> kEverySecret{
    SecretSpec{"postgres_dsn", SecretPurpose::kDatabase, ""},
    SecretSpec{"yookassa_shop_id", SecretPurpose::kPaymentProvider, "BILL"},
    SecretSpec{"yookassa_secret_key", SecretPurpose::kPaymentProvider, "BILL"},
    SecretSpec{"yookassa_webhook_secret", SecretPurpose::kWebhookSigning, "BILL"},
    SecretSpec{"livekit_api_key", SecretPurpose::kVideoProvider, "VIDEO"},
    SecretSpec{"livekit_api_secret", SecretPurpose::kVideoProvider, "VIDEO"},
};

std::optional<SecretPurpose> ParseSecretPurpose(std::string_view text);

}  // namespace pdr::core
