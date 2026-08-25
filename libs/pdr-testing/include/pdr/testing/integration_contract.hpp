#pragma once

#include <string>
#include <vector>

#include <gtest/gtest.h>

/// @file
/// Набор «внешнего сервиса нет и не будет»: ОДИН набор проверок,
/// инстанцируемый для каждой внешней интеграции.
///
/// Зачем он есть. Тест «внешний сервис вернул ошибку» проверяет обработку
/// ошибки. Он ничего не говорит о том, что будет, когда сервиса не станет
/// совсем: когда у него отзовут ключи, поменяют условия или он просто закроется.
/// Разница видна только на таком прогоне — продукт либо продолжает работать без
/// этой функции, либо оказывается несущим на чужом API, и тогда его удаление
/// стоит полтораста файлов (ADR-0014).
///
/// Поэтому проверяется не ошибка, а ОТСУТСТВИЕ: сервис не отвечает, не ответит и
/// не будет отвечать никогда.
///
/// Как подключить интеграцию: объявить «мир» и позвать PDR_INTEGRATION_CONTRACT.
///
/// @code
/// struct MyIntegrationWorld {
///     using Outcome = ...;                  // что возвращает сценарий продукта
///
///     void ServiceIsHealthy();              // сервис на месте и отвечает
///     void ServiceIsGoneForever();          // сервиса нет и не будет
///     void IntegrationSwitchedOff();        // флаг в динамическом конфиге снят
///
///     Outcome Run();                        // сценарий, ради которого продукт есть
///     int CallsOutside() const;             // сколько раз ходили наружу
///
///     static bool ProductWorked(const Outcome&);      // сценарий довёл дело до конца
///     static bool FeaturePresent(const Outcome&);     // была ли функция интеграции
///     static std::string Explanation(const Outcome&); // что видит человек вместо неё
/// };
///
/// PDR_INTEGRATION_CONTRACT(Receipts, MyIntegrationWorld);
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

/// Слова, которых человек в объяснении видеть не должен. Список короткий и
/// закрытый: он ловит не стиль, а подстановку технической строки вместо ответа.
inline const std::vector<std::string>& JargonInExplanation() {
    static const std::vector<std::string> words{
        "error",
        "exception",
        "timeout",
        "http",
        "500",
        "503",
        "null",
        "nullptr",
        "ПДР",
        "PDR",
        "api",
        "retry",
    };
    return words;
}

template<class World>
class IntegrationContract : public ::testing::Test {
protected:
    World world_;
};

PDR_CONTRACT_SUITE_P(IntegrationContract);

/// Оговорка ко всему набору: пока сервис на месте, функция есть. Без этой
/// проверки мир, у которого функции нет никогда, прошёл бы весь набор и не
/// проверил ничего.
PDR_CONTRACT_TEST_P(IntegrationContract, FeatureIsPresentWhileServiceIsHealthy) {
    this->world_.ServiceIsHealthy();

    const auto outcome = this->world_.Run();

    EXPECT_TRUE(TypeParam::ProductWorked(outcome)) << "сценарий не отработал на живом сервисе";
    EXPECT_TRUE(TypeParam::FeaturePresent(outcome))
        << "функции нет и при живом сервисе — набор проверял бы пустоту";
    EXPECT_GT(this->world_.CallsOutside(), 0) << "наружу не ходили: интеграция не подключена";
}

/// ГЛАВНОЕ: сервиса нет навсегда — продукт работает, функции нет.
PDR_CONTRACT_TEST_P(IntegrationContract, ProductWorksWhenServiceIsGoneForever) {
    this->world_.ServiceIsGoneForever();

    const auto outcome = this->world_.Run();

    ASSERT_TRUE(TypeParam::ProductWorked(outcome))
        << "продукт не работает без внешнего сервиса — значит интеграция несущая (ADR-0014)";
    EXPECT_FALSE(TypeParam::FeaturePresent(outcome))
        << "функция объявлена присутствующей, хотя сервиса нет";
}

/// Человеку сказано словами, чего нет. Пустое объяснение и техническая строка —
/// одно и то же: человек не узнал, что случилось.
PDR_CONTRACT_TEST_P(IntegrationContract, AbsenceIsExplainedInHumanWords) {
    this->world_.ServiceIsGoneForever();

    const auto explanation = TypeParam::Explanation(this->world_.Run());

    ASSERT_FALSE(explanation.empty()) << "человеку не сказано, почему функции нет";
    for (const auto& word : JargonInExplanation()) {
        EXPECT_EQ(explanation.find(word), std::string::npos)
            << "в объяснении для человека техническая строка: «" << word << "»";
    }
}

/// Отключение — флаг в динамическом конфиге, а не правка кода: выключенная
/// интеграция наружу не ходит вовсе, а продукт работает так же.
PDR_CONTRACT_TEST_P(IntegrationContract, SwitchedOffDoesNotCallOutside) {
    this->world_.ServiceIsHealthy();
    this->world_.IntegrationSwitchedOff();

    const auto outcome = this->world_.Run();

    EXPECT_TRUE(TypeParam::ProductWorked(outcome)) << "снятый флаг сломал продукт";
    EXPECT_FALSE(TypeParam::FeaturePresent(outcome)) << "флаг снят, а функция на месте";
    EXPECT_EQ(this->world_.CallsOutside(), 0)
        << "выключенная интеграция всё равно ходит наружу — флаг ничего не выключает";
}

/// Отсутствие не лечится повторами: сервиса нет навсегда, и продукт не обязан
/// это выяснять заново на каждом обращении.
PDR_CONTRACT_TEST_P(IntegrationContract, AbsenceIsNotRetriedForever) {
    this->world_.ServiceIsGoneForever();

    this->world_.Run();
    const auto after_first = this->world_.CallsOutside();
    this->world_.Run();
    this->world_.Run();

    EXPECT_LE(this->world_.CallsOutside(), after_first * 3)
        << "число обращений наружу растёт быстрее числа сценариев: отсутствие лечат повторами";
}

PDR_CONTRACT_REGISTER_P(IntegrationContract,
                        FeatureIsPresentWhileServiceIsHealthy,
                        ProductWorksWhenServiceIsGoneForever,
                        AbsenceIsExplainedInHumanWords,
                        SwitchedOffDoesNotCallOutside,
                        AbsenceIsNotRetriedForever);

}  // namespace pdr::testing

/// Инстанцировать набор для своей интеграции. Приставка — имя интеграции из
/// docs/architecture/integrations.md: по ней scripts/check_integrations.py
/// находит, что набор для интеграции заведён.
#define PDR_INTEGRATION_CONTRACT(prefix, world) \
    PDR_CONTRACT_INSTANTIATE_P(prefix, IntegrationContract, ::testing::Types<world>)
