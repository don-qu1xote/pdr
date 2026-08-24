#include <string>

#include <pdr/testing/integration_contract.hpp>

/// @file
/// ОТРИЦАТЕЛЬНЫЙ СЛУЧАЙ ко всей затее: интеграция, сделанная несущей, обязана
/// ронять набор.
///
/// Набор ценен ровно настолько, насколько он ловит несущую интеграцию. Проверить
/// это можно единственным способом: подсунуть мир, в котором продукт без чужого
/// сервиса не работает, и убедиться, что набор упал. Поэтому цель
/// pdr_broken_integration_contract_test помечена в ctest как WILL_FAIL: зелёный
/// прогон здесь означает, что набор ничего не проверяет.
///
/// Сломан он ровно так, как это случилось в проекте six-feat (ADR-0014): чужой
/// API стал обязательным источником данных, и без него сценарий не доходит до
/// конца.

namespace pdr::testing {
namespace {

struct Lesson final {
    bool booked{false};
    std::string room;
    std::string explanation;
};

class LoadBearingWorld final {
public:
    using Outcome = Lesson;

    void ServiceIsHealthy() {
        alive_ = true;
    }

    void ServiceIsGoneForever() {
        alive_ = false;
    }

    void IntegrationSwitchedOff() {
        enabled_ = false;
    }

    Outcome Run() {
        Lesson lesson;
        ++calls_;
        if (!alive_ || !enabled_) {
            lesson.explanation = "error: room provider unavailable (503)";
            return lesson;
        }
        lesson.booked = true;
        lesson.room = "https://video.example.test/room/1";
        return lesson;
    }

    int CallsOutside() const noexcept {
        return calls_;
    }

    static bool ProductWorked(const Outcome& outcome) {
        return outcome.booked;
    }

    static bool FeaturePresent(const Outcome& outcome) {
        return !outcome.room.empty();
    }

    static std::string Explanation(const Outcome& outcome) {
        return outcome.explanation;
    }

private:
    bool alive_{true};
    bool enabled_{true};
    int calls_{0};
};

}  // namespace

PDR_INTEGRATION_CONTRACT(LoadBearing, LoadBearingWorld);

}  // namespace pdr::testing
