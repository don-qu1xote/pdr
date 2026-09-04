/// @file
/// ЗАПИСЬ ЖУРНАЛА СОДЕРЖИТ ПОЛЯ, А НЕ ТЕКСТ С ВКЛЕЕННЫМИ ЗНАЧЕНИЯМИ.
///
/// Проверяется не намерение, а готовая запись: штатный `utest::LogCaptureLogger`
/// ловит то же, что ушло бы в файл, и `LogRecord::GetTag` спрашивает у неё поле
/// по имени. Тест, проверяющий текст сообщения, прошёл бы и на конкатенации.
#include "infrastructure/observe/log_fields.hpp"

#include <string>

#include <userver/logging/log.hpp>
#include <userver/tracing/span.hpp>
#include <userver/utest/log_capture_fixture.hpp>
#include <userver/utest/utest.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/observe/span_tags.hpp"

namespace pdr::infrastructure::observe {
namespace {

/// Формат ловца — tskv, а не kRaw по умолчанию: теги спана попадают в запись
/// на том же шаге, что и всё остальное её оформление, и ловец с сырым форматом
/// их не пишет вовсе.
class Journal : public userver::utest::DefaultLoggerFixture<::testing::Test> {
protected:
    Journal() {
        SetDefaultLogger(logger_.GetLogger());
    }

    userver::utest::LogCaptureLogger& GetLogCapture() {
        return logger_;
    }

private:
    userver::utest::LogCaptureLogger logger_{userver::logging::Format::kTskv};
};

constexpr std::string_view kTenant = "11111111-1111-1111-1111-111111111111";
constexpr std::string_view kActor = "22222222-2222-2222-2222-222222222222";

core::TenantId Tenant() {
    return core::TenantId::Parse(std::string{kTenant}).value();
}

core::PersonId Actor() {
    return core::PersonId::Parse(std::string{kActor}).value();
}

}  // namespace

/// ГДЕ ПРОВЕРЯЮТСЯ ТЕГИ СПАНА И ПОЧЕМУ НЕ ЗДЕСЬ.
///
/// `utest::LogCaptureLogger` собирает запись сам и тегов спана в неё не
/// переносит — ни `tenant_id`, ни даже штатный `trace_id`. Тест, написанный
/// здесь, проверял бы не постановку тега, а устройство ловца, и был бы красным
/// при исправно работающем коде.
///
/// Поэтому теги проверяются там, где запись собирает настоящий журнал: на
/// поднятом процессе, штатным `service_client.capture_logs()` —
/// services/scheduling/tests/test_journal.py. Здесь остаётся то, что видно без
/// процесса: постановка тега не падает без спана, и поля записи приходят с
/// именами из реестра.
UTEST_F(Journal, TaggingWithoutASpanIsNotAFailure) {
    TagTenant(Tenant());
    TagActor(Actor());
    LOG_CRITICAL() << "что-то произошло";

    const auto record = userver::utest::GetSingleLog(GetLogCapture().GetAll());

    EXPECT_EQ(record.GetText(), "что-то произошло");
}

/// ОГОВОРКА: имя, которого не ставили, не находится. Без неё следующий тест был
/// бы зелёным и в мире, где `GetTag` возвращает что угодно на любое имя.
UTEST_F(Journal, AFieldThatWasNotSetIsNotThere) {
    LOG_CRITICAL() << "что-то произошло";

    const auto record = userver::utest::GetSingleLog(GetLogCapture().GetAll());

    EXPECT_FALSE(record.GetTagOptional(kOutgoingDirectionField).has_value());
}

UTEST_F(Journal, ValuesGoIntoFieldsWithTheirRegisteredNames) {
    LOG_CRITICAL() << "направление не ответило"
                   << userver::logging::LogExtra{
                          {{kOutgoingDirectionField, std::string{"payments"}},
                           {kOutgoingFailureField, std::string{"Timeout was reached"}}}};

    const auto record = userver::utest::GetSingleLog(GetLogCapture().GetAll());

    EXPECT_EQ(record.GetTag(kOutgoingDirectionField), "payments");
    EXPECT_EQ(record.GetTag(kOutgoingFailureField), "Timeout was reached");
    EXPECT_EQ(record.GetText(), "направление не ответило");
}

}  // namespace pdr::infrastructure::observe
