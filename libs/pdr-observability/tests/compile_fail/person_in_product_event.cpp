#include "observability/contract.hpp"

/// @file
/// Эта цель ОБЯЗАНА не собираться.
///
/// «В продуктовом событии нет идентификатора человека» — свойство типа, а не
/// договорённость: `Value::Reference` не принимает `PersonId`. Убрать
/// ограничение и оставить правило в документе значит не иметь правила.

int main() {
    const auto person = pdr::core::PersonId::Parse("0e0e0e0e-0001-4000-8000-000000000001");
    const auto value = pdr::observability::Value::Reference(*person);
    return static_cast<int>(value.Kind());
}
