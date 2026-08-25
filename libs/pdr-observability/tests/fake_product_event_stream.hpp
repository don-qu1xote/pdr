#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "observability/application/ports/product_event_stream.hpp"

namespace pdr::observability::testing {

/// Поток в памяти. Он же — читатель: выгрузка отвечает на вопросы реестра, и
/// проверять запись, ни разу её не прочитав, значит проверять половину.
class FakeProductEventStream final : public ports::ProductEventStream {
public:
    void Record(const ProductEvent& event) override {
        recorded_.push_back(event);
    }

    const std::vector<ProductEvent>& Recorded() const noexcept {
        return recorded_;
    }

    std::optional<ProductEvent> Last(std::string_view type) const {
        for (auto entry = recorded_.rbegin(); entry != recorded_.rend(); ++entry) {
            if (entry->Type() == type) {
                return *entry;
            }
        }
        return std::nullopt;
    }

private:
    std::vector<ProductEvent> recorded_;
};

}  // namespace pdr::observability::testing
