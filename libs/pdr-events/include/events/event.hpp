#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>

#include "events/envelope.hpp"

namespace pdr::events {

/// Что обязано быть у типа события, чтобы попасть в реестр.
///
/// `kType` — стабильное имя вида «identity.guardianship_revoked». По нему
/// событие узнают в журнале, в очереди и в чужом коде, поэтому оно не меняется
/// вместе с именем структуры. `kVersion` — версия схемы: поле добавили —
/// версия та же, поле переименовали или убрали — новый тип с новой версией,
/// потому что старые подписчики никуда не делись.
template<class T>
concept Event = std::is_class_v<T> && requires(const T& event) {
    { T::kType } -> std::convertible_to<std::string_view>;
    { T::kVersion } -> std::convertible_to<int>;
    { event.envelope } -> std::convertible_to<Envelope>;
};

}  // namespace pdr::events
