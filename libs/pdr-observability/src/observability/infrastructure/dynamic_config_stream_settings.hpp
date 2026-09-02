#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

namespace pdr::observability {

/// Писать ли поток и сколько дней его держать.
///
/// Выключение — РЕШЕНИЕ, а не настройка по умолчанию: с выключенным потоком
/// вопросы из `docs/product/open-questions.md` перестают закрываться молча.
/// Поэтому смена значения обязана попадать в журнал: «а с какого дня у нас нет
/// данных» — вопрос, на который отвечают записью, а не памятью.
///
/// СТРУКТУРЫ ЗНАЧЕНИЯ ЗДЕСЬ НЕТ: она порождена из `PDR_PRODUCT_EVENTS` в
/// configs/dynamic/registry.yaml вместе с ключом, умолчанием и пределами.
class DynamicConfigStreamSettings final {
public:
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
