#pragma once

#include <string>
#include <string_view>

#include "observability/application/ports/product_event_stream.hpp"
#include "observability/contract.hpp"

namespace pdr::observability {

/// Публичный контракт поверх потока: собрать запись, проверить обезличивание,
/// отдать в порт.
///
/// Проверка стоит ЗДЕСЬ, на входе, а не у каждого издателя: правило одно, и
/// место, где оно применяется, тоже должно быть одно. Издатель, забывший про
/// правило, получает отказ, а не молчаливо записанный идентификатор.
class ContractService final : public Contract {
public:
    explicit ContractService(ports::ProductEventStream& stream) noexcept;

    core::Result<void> Record(const core::TenantId& tenant,
                              std::string_view type,
                              int version,
                              Role actor,
                              core::Instant occurred_at,
                              Fields fields) override;

private:
    ports::ProductEventStream& stream_;
};

}  // namespace pdr::observability
