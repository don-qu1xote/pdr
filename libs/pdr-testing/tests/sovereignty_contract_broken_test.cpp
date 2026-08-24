#include <set>
#include <string>
#include <utility>

#include <pdr/testing/sovereignty_contract.hpp>

/// @file
/// ОТРИЦАТЕЛЬНЫЙ СЛУЧАЙ ко всей затее: продукт, которому для работы нужна сеть,
/// обязан ронять сквозной прогон.
///
/// Набор ценен ровно настолько, насколько он ловит жёсткую зависимость.
/// Проверить это можно единственным способом: подсунуть сборку, у которой без
/// сети пропадает конспект, и убедиться, что прогон упал. Поэтому цель
/// pdr_network_bound_contract_test помечена в ctest как WILL_FAIL: зелёный
/// прогон здесь означает, что набор ничего не проверяет.
///
/// Сломана она так, как ломается на самом деле: каждая интеграция по
/// отдельности честно деградирует — занятие назначается, подбор работает, — а
/// конспекта без внешней модели просто нет. По ADR-0014 нарушения не видно, по
/// ADR-0015 продукт нерабочий.

namespace pdr::testing {
namespace {

struct Result final {
    bool booked{false};
    std::string note;
    int offered{0};
};

class NetworkBoundWorld final {
public:
    using Outcome = Result;

    void NetworkIsOff() {
        network_ = false;
    }

    void NetworkIsOn() {
        network_ = true;
    }

    void NodeSwitchedToOwn(std::string node) {
        own_.insert(std::move(node));
    }

    Outcome Run() {
        Result result;
        result.booked = true;
        result.offered = 3;
        if (network_ && !own_.count("text_generation")) {
            ++calls_;
            result.note =
                "Занятие прошло, разобрали тему, дома три задания на тот же приём, "
                "ошибка повторяется в переносе знака при выносе общего множителя.";
        }
        return result;
    }

    int CallsOutside() const noexcept {
        return calls_;
    }

    static bool LessonBooked(const Outcome& outcome) {
        return outcome.booked;
    }

    static std::string Note(const Outcome& outcome) {
        return outcome.note;
    }

    static int OfferedExercises(const Outcome& outcome) {
        return outcome.offered;
    }

private:
    bool network_{true};
    std::set<std::string> own_;
    int calls_{0};
};

}  // namespace

PDR_SOVEREIGNTY_CONTRACT(NetworkBound, NetworkBoundWorld);

}  // namespace pdr::testing
