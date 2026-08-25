#pragma once

#include <functional>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include "events/event.hpp"

namespace pdr::events {

/// Шина доменных событий: издатель не знает подписчиков и не должен узнать.
///
/// Ради этого всё и затевалось. Подписчик добавляется в СВОЁМ модуле — берёт
/// тип события из этого реестра и регистрирует обработчик. Файлы издателя при
/// этом не меняются ни одной строкой: у него нет ни списка получателей, ни
/// вызова «а теперь оповестить почту».
///
/// Публикация и подписка типизированы; стирание типа спрятано за protected —
/// виртуальных шаблонов не бывает, а реализация шины должна оставаться одна.
class Bus {
public:
    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    virtual ~Bus() = default;

    template<Event E>
    void Publish(const E& event) {
        PublishErased(std::type_index{typeid(E)}, &event);
    }

    template<Event E>
    void Subscribe(std::function<void(const E&)> handler) {
        SubscribeErased(std::type_index{typeid(E)},
                        [handler = std::move(handler)](const void* event) {
                            handler(*static_cast<const E*>(event));
                        });
    }

protected:
    using ErasedHandler = std::function<void(const void*)>;

    Bus() = default;

    virtual void PublishErased(std::type_index type, const void* event) = 0;
    virtual void SubscribeErased(std::type_index type, ErasedHandler handler) = 0;
};

}  // namespace pdr::events
