#include <set>
#include <string>
#include <utility>

#include <pdr/testing/sovereignty_contract.hpp>

/// @file
/// Образцовая сборка продукта для набора «работаем без интернета»: она
/// показывает, как подключается сквозной прогон, и доказывает, что набор
/// проходим на одних своих моделях.
///
/// НАСТОЯЩЕЙ СБОРКИ ПРОДУКТА В ДЕРЕВЕ ПОКА НЕТ. Из трёх шагов сценария в
/// `libs/` есть только первый: `scheduling::BookLesson`. Контекстов `notes`,
/// `practice`, `recommend`, `media` и `ml` нет вовсе — они названы на карте
/// контекстов и рождаются в фазах 2–4. Поэтому набор проверяется на образце.
///
/// Требование инстанцировать его настоящей сборкой держит
/// docs/architecture/ai-sovereignty.md: сквозной прогон без сети — условие
/// приёмки каждого ИИ-узла (ADR-0015), и первый же контекст, который такой узел
/// заводит, обязан заменить этот образец своим миром.
///
/// Образец считает своими средствами: конспект собирается из фактов занятия
/// шаблоном, задания подбираются простым правилом. Ровно так карта контекстов
/// описывает выдачу до фазы 4 — «простым правилом внутри practice».

namespace pdr::testing {
namespace {

struct Result final {
    bool booked{false};
    std::string note;
    int offered{0};
};

class OwnModelsWorld final {
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
        result.note = Note();
        result.offered = 3;
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
    std::string Note() {
        if (network_ && !own_.count("text_generation")) {
            ++calls_;
            return "Занятие по теме «Квадратные уравнения» прошло двадцать пятого августа. "
                   "Разобрали дискриминант и теорему Виета, решили шесть примеров из семи, "
                   "ошибка повторилась в переносе знака при выносе множителя. На дом "
                   "оставлены три задания на тот же приём.";
        }
        return "Занятие по теме «Квадратные уравнения» прошло двадцать пятого августа. "
               "Разобрано: дискриминант, теорема Виета. Решено шесть примеров из семи. "
               "Повторяющаяся ошибка: перенос знака. На дом: три задания.";
    }

    bool network_{true};
    std::set<std::string> own_;
    int calls_{0};
};

}  // namespace

PDR_SOVEREIGNTY_CONTRACT(Example, OwnModelsWorld);

}  // namespace pdr::testing
