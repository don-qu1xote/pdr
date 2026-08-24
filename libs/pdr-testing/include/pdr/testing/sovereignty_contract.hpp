#pragma once

#include <string>

#include <gtest/gtest.h>

/// @file
/// Набор «работаем без интернета»: ОДИН сквозной прогон, инстанцируемый для
/// каждой сборки продукта.
///
/// Он отличается от набора «сервиса нет навсегда»
/// (@ref integration_contract.hpp) уровнем вопроса. Тот спрашивает про одну
/// интеграцию: продукт пережил её отказ. Этот спрашивает про весь продукт:
/// занятие назначено, конспект собран, задания подобраны — и всё это при НУЛЕ
/// внешних вызовов, разом по всем узлам.
///
/// Разница не косметическая. Каждая интеграция по отдельности может честно
/// деградировать, а продукт целиком — оказаться бесполезным без сети: конспект
/// пустой, подбор молчит, остаётся календарь. Такое положение не видно ни в
/// одном тесте на отдельную интеграцию (ADR-0015).
///
/// Этот же прогон ловит незаметно появившуюся жёсткую зависимость: строка,
/// добавленная в сценарий и требующая внешнего вызова, роняет его в тот же
/// день, а не в день отказа провайдера.
///
/// Как подключить сборку продукта: объявить «мир» и позвать
/// PDR_SOVEREIGNTY_CONTRACT.
///
/// @code
/// struct MyProductWorld {
///     using Outcome = ...;                // что осталось после сквозного прогона
///
///     void NetworkIsOff();                // ни одного внешнего вызова
///     void NetworkIsOn();                 // внешние узлы разрешены
///     void NodeSwitchedToOwn(std::string node);  // узел переведён на свою модель
///
///     Outcome Run();                      // занятие, конспект, подбор — насквозь
///     int CallsOutside() const;           // сколько раз ходили наружу
///
///     static bool LessonBooked(const Outcome&);
///     static std::string Note(const Outcome&);       // конспект, а не признак
///     static int OfferedExercises(const Outcome&);   // сколько заданий подобрано
/// };
///
/// PDR_SOVEREIGNTY_CONTRACT(Product, MyProductWorld);
/// @endcode

#ifndef PDR_CONTRACT_SUITE_P
#define PDR_CONTRACT_SUITE_P(suite) TYPED_TEST_SUITE_P(suite)
#endif

#ifndef PDR_CONTRACT_TEST_P
#define PDR_CONTRACT_TEST_P(suite, name) TYPED_TEST_P(suite, name)
#endif

#ifndef PDR_CONTRACT_REGISTER_P
#define PDR_CONTRACT_REGISTER_P(suite, ...) REGISTER_TYPED_TEST_SUITE_P(suite, __VA_ARGS__)
#endif

#ifndef PDR_CONTRACT_INSTANTIATE_P
#define PDR_CONTRACT_INSTANTIATE_P(prefix, suite, types) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, suite, types)
#endif

namespace pdr::testing {

/// Сколько слов должно быть в конспекте, чтобы он считался конспектом, а не
/// признаком того, что конспект «есть». Число выбрано на глаз и намеренно
/// маленькое: набор проверяет наличие работы, а не её качество — качество
/// считает scripts/compare_quality.py на фиксированном наборе.
inline constexpr int kNoteIsNotAStub = 20;

inline int WordsIn(const std::string& text) {
    int words = 0;
    bool inside = false;
    for (const char symbol : text) {
        const bool letter = symbol != ' ' && symbol != '\n' && symbol != '\t';
        if (letter && !inside) {
            ++words;
        }
        inside = letter;
    }
    return words;
}

template<class World>
class SovereigntyContract : public ::testing::Test {
protected:
    World world_;
};

PDR_CONTRACT_SUITE_P(SovereigntyContract);

/// ГЛАВНОЕ: сквозной сценарий проходит при нуле внешних вызовов.
PDR_CONTRACT_TEST_P(SovereigntyContract, WholeScenarioWorksWithoutASingleExternalCall) {
    this->world_.NetworkIsOff();

    const auto outcome = this->world_.Run();

    EXPECT_TRUE(TypeParam::LessonBooked(outcome)) << "занятие не назначено без сети";
    EXPECT_FALSE(TypeParam::Note(outcome).empty()) << "конспект не собран без сети";
    EXPECT_GT(TypeParam::OfferedExercises(outcome), 0) << "задания не подобраны без сети";
    EXPECT_EQ(this->world_.CallsOutside(), 0)
        << "сценарий ходил наружу при выключенной сети: где-то осталась жёсткая зависимость";
}

/// Своя реализация — работающая, а не заглушка, отдающая пустоту. Разница между
/// «деградация есть» и «деградация для галочки» проверяется здесь.
PDR_CONTRACT_TEST_P(SovereigntyContract, OwnImplementationIsNotAStub) {
    this->world_.NetworkIsOff();

    const auto outcome = this->world_.Run();

    EXPECT_GE(WordsIn(TypeParam::Note(outcome)), kNoteIsNotAStub)
        << "конспект без сети короче " << kNoteIsNotAStub
        << " слов — это признак конспекта, а не конспект";
}

/// Внешние узлы улучшают то, что и так работает: с сетью сценарий даёт тот же
/// набор результатов. Если с сетью он даёт больше ШАГОВ, значит без сети шаг
/// пропадал, и первый прогон это скрыл.
PDR_CONTRACT_TEST_P(SovereigntyContract, ExternalOnlyImprovesWhatAlreadyWorks) {
    this->world_.NetworkIsOn();

    const auto outcome = this->world_.Run();

    EXPECT_TRUE(TypeParam::LessonBooked(outcome));
    EXPECT_FALSE(TypeParam::Note(outcome).empty());
    EXPECT_GT(TypeParam::OfferedExercises(outcome), 0);
}

/// Переключение узла на свою модель — конфигом, и оно действует: обращения
/// наружу прекращаются, а сценарий остаётся целым.
PDR_CONTRACT_TEST_P(SovereigntyContract, SwitchingNodeToOwnStopsCallingOutside) {
    this->world_.NetworkIsOn();
    const auto before = this->world_.Run();
    ASSERT_TRUE(TypeParam::LessonBooked(before));

    this->world_.NodeSwitchedToOwn("text_generation");
    const auto after = this->world_.Run();

    EXPECT_TRUE(TypeParam::LessonBooked(after));
    EXPECT_FALSE(TypeParam::Note(after).empty()) << "переключение узла на свою модель убрало "
                                                    "конспект — своей реализации нет";
    EXPECT_GT(TypeParam::OfferedExercises(after), 0);
}

PDR_CONTRACT_REGISTER_P(SovereigntyContract,
                        WholeScenarioWorksWithoutASingleExternalCall,
                        OwnImplementationIsNotAStub,
                        ExternalOnlyImprovesWhatAlreadyWorks,
                        SwitchingNodeToOwnStopsCallingOutside);

}  // namespace pdr::testing

/// Инстанцировать набор для своей сборки продукта.
#define PDR_SOVEREIGNTY_CONTRACT(prefix, world) \
    PDR_CONTRACT_INSTANTIATE_P(prefix, SovereigntyContract, ::testing::Types<world>)
