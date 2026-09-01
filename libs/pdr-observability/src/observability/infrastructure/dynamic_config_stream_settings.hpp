#pragma once

#include <cstdint>
#include <string_view>

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/to.hpp>

namespace pdr::observability {

struct ProductEventsConfig final {
    bool enabled{};
    std::int32_t retention_days{};
};

extern const userver::dynamic_config::Key<ProductEventsConfig> kProductEvents;

ProductEventsConfig Parse(const userver::formats::json::Value& value,
                          userver::formats::parse::To<ProductEventsConfig>);

/// Писать ли поток и сколько дней его держать.
///
/// Выключение — РЕШЕНИЕ, а не настройка по умолчанию: с выключенным потоком
/// вопросы из `docs/product/open-questions.md` перестают закрываться молча.
/// Поэтому смена значения обязана попадать в журнал: «а с какого дня у нас нет
/// данных» — вопрос, на который отвечают записью, а не памятью.
class DynamicConfigStreamSettings final {
public:
    static constexpr std::string_view kProductEventsVariable = "PDR_PRODUCT_EVENTS";

    explicit DynamicConfigStreamSettings(userver::dynamic_config::Source source);

    ~DynamicConfigStreamSettings();

    DynamicConfigStreamSettings(const DynamicConfigStreamSettings&) = delete;
    DynamicConfigStreamSettings& operator=(const DynamicConfigStreamSettings&) = delete;

    bool Enabled() const;

private:
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::observability
