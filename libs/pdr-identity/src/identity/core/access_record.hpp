#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

/// Что именно смотрели. Список закрыт и повторяет ADR-0006: содержание
/// занятия, а не всё подряд. Журнал обо всём — это журнал, который никто не
/// читает.
enum class ResourceKind : std::uint8_t {
    kRecording,
    kTranscript,
    kChat,
};

/// Код вида: то же слово, что в `identity_access_log_kind_known`.
std::string_view Name(ResourceKind kind) noexcept;

std::optional<ResourceKind> ParseResourceKind(std::string_view text);

/// Строка журнала: кто, чьё и когда смотрел.
///
/// Право смотреть — не то же самое, что право смотреть незаметно. Опекун может
/// иметь доступ к записи занятия и всё равно оставить след: след видят и
/// ученик, и репетитор.
///
/// Своё чтение не журналируется: незаметно смотрят чужое, а не собственное.
/// Отказ, а не молчаливый пропуск, — чтобы точка записи не решала это сама.
class AccessRecord final {
public:
    static core::Result<AccessRecord> Of(core::TenantId tenant,
                                         core::PersonId actor,
                                         core::PersonId subject,
                                         ResourceKind kind,
                                         core::Instant at);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Actor() const noexcept {
        return actor_;
    }
    const core::PersonId& Subject() const noexcept {
        return subject_;
    }
    ResourceKind Kind() const noexcept {
        return kind_;
    }
    core::Instant At() const noexcept {
        return at_;
    }

    friend bool operator==(const AccessRecord&, const AccessRecord&) = default;

private:
    AccessRecord(core::TenantId tenant,
                 core::PersonId actor,
                 core::PersonId subject,
                 ResourceKind kind,
                 core::Instant at) noexcept
        : tenant_{std::move(tenant)},
          actor_{std::move(actor)},
          subject_{std::move(subject)},
          kind_{kind},
          at_{at} {}

    core::TenantId tenant_;
    core::PersonId actor_;
    core::PersonId subject_;
    ResourceKind kind_;
    core::Instant at_;
};

}  // namespace pdr::identity
