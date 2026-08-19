// Этот файл ОБЯЗАН не собираться: неявного превращения идентификатора в строку
// нет, текст выдаётся только явным ToString().
#include <string>

#include "core/types/ids.hpp"

int main() {
    const auto person = pdr::core::PersonId::FromBytes(pdr::core::IdBytes{});

    const std::string text = person;
    return static_cast<int>(text.size());
}
