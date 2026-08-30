/// @file
/// Цель ОБЯЗАНА не собираться: секрет не выводится никуда. Вывод удалён
/// шаблоном, поэтому не собирается и печать в поток стандартной библиотеки, и
/// печать в журнал — приёмник журнала здесь изображён своим типом, потому что
/// цель линкуется только с pdr_core, у которого userver нет.
#include <sstream>

#include "core/secret_string.hpp"

namespace {

struct Journal final {
    template<class T>
    Journal& operator<<(const T&) {
        return *this;
    }
};

}  // namespace

int main() {
    const pdr::core::SecretString secret{"настоящий-секрет"};

    std::ostringstream out;
    out << secret;

    Journal journal;
    journal << secret;

    return static_cast<int>(out.str().size());
}
