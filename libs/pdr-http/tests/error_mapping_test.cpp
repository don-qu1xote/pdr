#include "infrastructure/http/error_mapping.hpp"

#include <set>
#include <string>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/utest/utest.hpp>

#include "infrastructure/http/problem.hpp"

namespace pdr::infrastructure::http {
namespace {

constexpr Occasion kOccasion{"/lessons", "req-1"};

userver::formats::json::Value Rendered(const Problem& problem) {
    return userver::formats::json::FromString(Render(problem));
}

}  // namespace

/// ГЛАВНЫЙ ТЕСТ ЗАДАЧИ: форма отказа одна, и она есть у КАЖДОГО рода доменной
/// ошибки.
///
/// Перебор по всему реестру родов, а не по трём знакомым: род, заведённый
/// завтра и забытый в отображении, обязан упасть здесь, а не отдать человеку
/// пятисотую в проде.
UTEST(ErrorMapping, EveryKindOfDomainErrorGetsTheSameShape) {
    for (const auto kind : core::kEveryErrorKind) {
        const core::Error error{kind, "some_code", "подробность"};
        const auto problem = AsProblem(error, kOccasion);
        const auto body = Rendered(problem);

        EXPECT_EQ(problem.type, "urn:pdr:error:some_code") << core::Name(kind);
        EXPECT_FALSE(problem.title.empty()) << core::Name(kind);
        EXPECT_EQ(problem.detail, "подробность") << core::Name(kind);
        EXPECT_EQ(problem.instance, "/lessons") << core::Name(kind);
        EXPECT_EQ(problem.request_id, "req-1") << core::Name(kind);

        EXPECT_GE(problem.status, 400) << core::Name(kind) << ": отказ отдали успехом";
        EXPECT_LT(problem.status, 500)
            << core::Name(kind) << ": ожидаемый отказ отдали как нашу поломку";

        for (const auto* member : {"type", "title", "status", "detail", "instance", "request_id"}) {
            EXPECT_TRUE(body.HasMember(member))
                << core::Name(kind) << ": в теле отказа нет «" << member << "»";
        }
        EXPECT_EQ(body["status"].As<int>(), problem.status) << core::Name(kind);
    }
}

UTEST(ErrorMapping, KindsDoNotShareAStatus) {
    std::set<int> seen;
    for (const auto kind : core::kEveryErrorKind) {
        EXPECT_TRUE(seen.insert(StatusOf(kind)).second)
            << core::Name(kind) << ": два рода отказа отвечают одним статусом, и клиенту "
            << "приходится разбирать код там, где хватило бы статуса";
    }
}

UTEST(ErrorMapping, EachKindKeepsItsAgreedStatus) {
    EXPECT_EQ(StatusOf(core::ErrorKind::kValidation), 422);
    EXPECT_EQ(StatusOf(core::ErrorKind::kNotFound), 404);
    EXPECT_EQ(StatusOf(core::ErrorKind::kConflict), 409);
    EXPECT_EQ(StatusOf(core::ErrorKind::kForbidden), 403);
}

/// Отказ политики — тоже отказ, и форма у него та же.
UTEST(ErrorMapping, EveryDenyReasonGetsTheSameShape) {
    for (const auto reason : identity::kEveryDenyReason) {
        if (reason == identity::DenyReason::kAllowed) {
            continue;
        }

        const auto problem = AsProblem(identity::Denied(reason), kOccasion);
        const auto body = Rendered(problem);

        EXPECT_EQ(problem.type, "urn:pdr:error:" + std::string{identity::Name(reason)});
        EXPECT_EQ(problem.request_id, "req-1") << identity::Name(reason);
        EXPECT_GE(problem.status, 400) << identity::Name(reason);
        EXPECT_TRUE(body.HasMember("request_id")) << identity::Name(reason);
    }
}

/// Чужой кабинет отвечает «не найдено», а не «нельзя»: 403 подтвердил бы, что
/// ресурс есть, и перебор идентификаторов стал бы способом узнать про соседа.
UTEST(ErrorMapping, AForeignTenantIsNotFoundRatherThanForbidden) {
    EXPECT_EQ(StatusOf(identity::DenyReason::kForeignTenant), 404);
    EXPECT_EQ(StatusOf(identity::DenyReason::kNotYours), 403);
}

/// Действие без политики — наша поломка настройки, а не отказ человеку. В
/// четырёхсотых её никто не заметит.
UTEST(ErrorMapping, AnActionWithoutAPolicyIsOurBreakageAndSaysSo) {
    EXPECT_EQ(StatusOf(identity::DenyReason::kNoPolicy), 500);
}

/// «Кто вы» и «вам нельзя» — разные новости: по первой входят заново, по второй
/// входить бесполезно. Родом доменной ошибки это не выражается — сессия
/// отвечает kNotFound и kForbidden, и по ним 401 не собрать.
UTEST(ErrorMapping, NotBeingIdentifiedIsNotTheSameAsNotBeingAllowed) {
    const core::Error expired{core::ErrorKind::kForbidden, "session_expired", "срок сессии вышел"};

    EXPECT_EQ(Unidentified(expired, kOccasion).status, 401);
    EXPECT_EQ(AsProblem(expired, kOccasion).status, 403);
    EXPECT_EQ(Unidentified(expired, kOccasion).type, "urn:pdr:error:session_expired");
}

/// Разбор тела не дошёл до правил домена, поэтому 400, а не 422.
UTEST(ErrorMapping, AMalformedBodyNamesTheFieldAndAnswersFourHundred) {
    const core::Error broken{core::ErrorKind::kValidation, "request_field_invalid", "не число"};
    const auto problem = Malformed(broken, "/minutes", kOccasion);

    EXPECT_EQ(problem.status, 400);
    ASSERT_TRUE(problem.field.has_value());
    EXPECT_EQ(*problem.field, "/minutes");
    EXPECT_EQ(Rendered(problem)["field"].As<std::string>(), "/minutes");
}

UTEST(ErrorMapping, TheFieldIsAbsentWhenThereIsNoField) {
    const core::Error error{core::ErrorKind::kConflict, "slot_already_taken", "занято"};

    EXPECT_FALSE(AsProblem(error, kOccasion).field.has_value());
    EXPECT_FALSE(Rendered(AsProblem(error, kOccasion)).HasMember("field"));
}

}  // namespace pdr::infrastructure::http
