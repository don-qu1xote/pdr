#include "testing/check.hpp"

#include <iostream>

namespace pdr::testing {
namespace {

int failures = 0;
int checks = 0;

}  // namespace

void Check(bool condition, std::string_view expression, std::string_view file, int line) {
    ++checks;
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << file << ":" << line << ": не выполнено: " << expression << "\n";
}

int Summary(std::string_view suite) {
    if (failures == 0) {
        std::cout << suite << ": проверок " << checks << ", все прошли\n";
        return 0;
    }
    std::cerr << suite << ": проверок " << checks << ", не прошли " << failures << "\n";
    return 1;
}

}  // namespace pdr::testing
