#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <pdr/api/openapi.hpp>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/utest/utest.hpp>

#include "infrastructure/http/openapi_document.hpp"

namespace pdr::api {
namespace {

using userver::formats::json::Value;

const std::string kDocument = std::string{PDR_SOURCE_DIR} + "/docs/api/openapi.yaml";

/// КРУГ: пример из схемы → структура → снова JSON.
///
/// Оба конца порождены из одной схемы, поэтому круг проверяет ровно то, ради
/// чего порождение затевалось: тип умеет прочесть то, что спецификация обещает
/// клиенту, и написать обратно то же самое.
template<class T>
Value Circle(const Value& example) {
    return userver::formats::json::ValueBuilder{Parse(example, userver::formats::parse::To<T>{})}
        .ExtractValue();
}

/// СВЕРКА РЕЕСТРА С ДОКУМЕНТОМ: каждая схема названа здесь ровно один раз.
///
/// Список написан руками НАРОЧНО, и он же — вторая половина проверки: схема,
/// добавленная в спецификацию и забытая здесь, роняет прогон. Обойти список и
/// «проверить всё, что найдётся» значило бы не проверять ничего: забытую схему
/// такой обход тоже пропустил бы молча.
struct Round final {
    std::string_view name;
    Value (*circle)(const Value&);
};

const Round kRound[] = {
    {"Uuid", &Circle<Uuid>},
    {"RequestIdValue", &Circle<RequestIdValue>},
    {"Liveness", &Circle<Liveness>},
    {"Readiness", &Circle<Readiness>},
    {"SignInRequest", &Circle<SignInRequest>},
    {"SignInAnswer", &Circle<SignInAnswer>},
    {"Problem", &Circle<Problem>},
    {"ProblemType", &Circle<ProblemType>},
    {"Moment", &Circle<Moment>},
    {"Zone", &Circle<Zone>},
    {"CalendarDate", &Circle<CalendarDate>},
    {"ClockTime", &Circle<ClockTime>},
    {"Minutes", &Circle<Minutes>},
    {"Weekday", &Circle<Weekday>},
    {"LessonSide", &Circle<LessonSide>},
    {"LessonState", &Circle<LessonState>},
    {"Nothing", &Circle<Nothing>},
    {"AvailabilityRule", &Circle<AvailabilityRule>},
    {"AvailabilityException", &Circle<AvailabilityException>},
    {"Availability", &Circle<Availability>},
    {"Lesson", &Circle<Lesson>},
    {"Lessons", &Circle<Lessons>},
    {"NewLesson", &Circle<NewLesson>},
    {"NewSeries", &Circle<NewSeries>},
    {"Series", &Circle<Series>},
};

Value Schemas() {
    const auto document = infrastructure::http::OpenApiDocument::FromFile(kDocument);
    EXPECT_TRUE(document.HasValue()) << kDocument;
    return userver::formats::json::FromString(document.Value().Json())["components"]["schemas"];
}

}  // namespace

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: пример из спецификации проходит круг без потерь.
UTEST(SchemaRoundTrip, EveryExampleSurvivesTheCircle) {
    const auto schemas = Schemas();

    for (const auto& round : kRound) {
        const auto named = schemas[std::string{round.name}];
        const auto example = named["example"];

        ASSERT_FALSE(example.IsMissing())
            << "у схемы «" << round.name
            << "» нет примера. Схема без примера не проверена ничем: круг разбора и "
               "сборки прогнать не на чем";

        EXPECT_EQ(round.circle(example), example)
            << "круг потерял или переиначил пример схемы «" << round.name << "»";
    }
}

/// Реестр и документ сходятся ПОИМЁННО. Иначе список тихо отстаёт от схемы, а
/// проверка выше продолжает зеленеть на том, что в нём осталось.
UTEST(SchemaRoundTrip, TheRegistryNamesEverySchemaInTheDocument) {
    std::vector<std::string> named;
    for (const auto& round : kRound) {
        named.emplace_back(round.name);
    }

    std::vector<std::string> written;
    for (auto schema = Schemas().begin(); schema != Schemas().end(); ++schema) {
        written.emplace_back(schema.GetName());
    }

    std::sort(named.begin(), named.end());
    std::sort(written.begin(), written.end());

    EXPECT_EQ(named, written) << "реестр круга разошёлся с components/schemas спецификации";
}

}  // namespace pdr::api
