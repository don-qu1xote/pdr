#include "infrastructure/http/authorized_handler.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <userver/engine/async.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/utest/utest.hpp>

#include "builders/identifiers.hpp"
#include "fake_idempotency_keys.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_tenant_aware_repository.hpp"

namespace pdr::infrastructure::http {
namespace {

using pdr::http::testing::FakeIdempotencyKeys;
using pdr::testing::FakeClock;
using pdr::testing::FakeTenantAwareRepository;
using pdr::testing::FakeTenantSession;
using pdr::testing::Numbered;

const auto kTenant = Numbered<core::TenantId>(1);
const auto kActor = Numbered<core::PersonId>(2);
const auto kStudent = Numbered<core::PersonId>(3);

constexpr std::string_view kCookie = "__Host-pdr_session";
constexpr std::string_view kAuthorization = "Authorization";

const std::string kSchemaFile =
    std::string{PDR_SOURCE_DIR} + "/libs/pdr-http/tests/schemas/book_lesson.json";

const std::string kGoodBody =
    R"({"student_id": "s-1", "starts_at": "2026-09-01T10:00:00Z", "minutes": 45})";

const std::string kOtherBody =
    R"({"student_id": "s-2", "starts_at": "2026-09-01T10:00:00Z", "minutes": 45})";

constexpr std::string_view kKey = "idempotency-0a1b2c3d";

/// Двойник ответа: ровно те методы, которыми пользуется хендлер.
class Answer final {
public:
    void SetStatus(userver::server::http::HttpStatus value) noexcept {
        status = static_cast<int>(value);
    }

    void SetHeader(std::string name, std::string value) {
        headers[std::move(name)] = std::move(value);
    }

    int status{200};
    std::map<std::string, std::string> headers;
};

/// Двойник запроса.
class Ask final {
public:
    Ask& WithHeader(std::string name, std::string value) {
        headers_[std::move(name)] = std::move(value);
        return *this;
    }

    Ask& WithCookie(std::string name, std::string value) {
        cookies_[std::move(name)] = std::move(value);
        return *this;
    }

    Ask& WithBody(std::string body) {
        body_ = std::move(body);
        return *this;
    }

    Ask& Changing(pdr::http::Method method) {
        method_ = method;
        return *this;
    }

    Ask& WithKey(std::string_view key) {
        return WithHeader(std::string{kIdempotencyKeyHeader}, std::string{key});
    }

    pdr::http::Method GetMethod() const noexcept {
        return method_;
    }

    const std::string& GetHeader(const std::string& name) const {
        return Found(headers_, name);
    }

    const std::string& GetCookie(const std::string& name) const {
        return Found(cookies_, name);
    }

    const std::string& RequestBody() const {
        return body_;
    }

    const std::string& GetRequestPath() const {
        return path_;
    }

    Answer& GetHttpResponse() const noexcept {
        return answer_;
    }

private:
    const std::string& Found(const std::map<std::string, std::string>& where,
                             const std::string& name) const {
        const auto found = where.find(name);
        return found == where.end() ? nothing_ : found->second;
    }

    std::map<std::string, std::string> headers_;
    std::map<std::string, std::string> cookies_;
    std::string body_{kGoodBody};
    std::string path_{"/lessons"};
    pdr::http::Method method_{pdr::http::Method::kGet};
    const std::string nothing_;
    mutable Answer answer_;
};

/// Двойник опознания: отвечает тем, что ему велели, и записывает, о чём
/// спросили.
class Knows final : public Callers {
public:
    CredentialSource Where() const override {
        return CredentialSource{kCookie, kAuthorization};
    }

    core::Result<Caller> Identify(std::string_view cookie, std::string_view header) const override {
        saw_cookie = std::string{cookie};
        saw_header = std::string{header};
        if (refuse.has_value()) {
            return *refuse;
        }
        return Caller{kTenant, kActor};
    }

    std::optional<core::Error> refuse;
    mutable std::string saw_cookie;
    mutable std::string saw_header;
};

/// Двойник политики: отвечает заранее заданным решением.
class Decides final : public identity::Contract {
public:
    bool MayActFor(const core::TenantId&,
                   const core::PersonId&,
                   const core::PersonId&) const override {
        return true;
    }

    identity::PolicyDecision Decide(const core::TenantId& tenant,
                                    const core::PersonId& actor,
                                    identity::Action action,
                                    const identity::Resource& resource) const override {
        asked.push_back(action);
        saw_tenant = tenant;
        saw_actor = actor;
        saw_subject = resource.subject;
        return answer;
    }

    identity::PolicyDecision answer{identity::Allowed()};
    mutable std::vector<identity::Action> asked;
    mutable std::optional<core::TenantId> saw_tenant;
    mutable std::optional<core::PersonId> saw_actor;
    mutable std::optional<core::PersonId> saw_subject;
};

/// Ручка, у которой нет ни одного предметного правила: она называет действие,
/// называет ресурс и зовёт сценарий. Больше в хендлере быть нечему.
class BookingHandler final : public AuthorizedHandler<Ask, FakeTenantSession> {
public:
    BookingHandler(const Callers& callers,
                   Database& database,
                   const identity::Contract& permissions,
                   Keys& keys,
                   const application::ports::Clock& clock,
                   pdr::http::KeyLifetime lifetime,
                   RequestSchema schema)
        : AuthorizedHandler{
              callers, database, permissions, keys, clock, lifetime, std::move(schema)} {}

    mutable std::atomic<int> ran{0};
    std::optional<core::Error> refuse;
    core::Instant::Duration takes{};

private:
    identity::Action Wants() const override {
        return identity::Action::kBookLesson;
    }

    identity::Resource About(const Caller& caller,
                             const userver::formats::json::Value&) const override {
        return identity::Resource{caller.tenant, std::nullopt, kStudent};
    }

    core::Result<userver::formats::json::Value> Run(const Call& call) const override {
        ++ran;
        if (takes.count() != 0) {
            userver::engine::SleepFor(takes);
        }
        if (refuse.has_value()) {
            return *refuse;
        }
        call.session.Insert(call.request_id);

        userver::formats::json::ValueBuilder made{userver::formats::json::Type::kObject};
        made["minutes"] = call.body["minutes"].As<int>();
        made["tenant"] = call.session.Tenant().ToString();
        return made.ExtractValue();
    }
};

/// Мир одной проверки: хендлер со всеми двойниками наготове.
struct World final {
    World()
        : schema{RequestSchema::FromFile(kSchemaFile)},
          handler{callers, database, permissions, keys, clock, Lifetime(), Schema()} {}

    static pdr::http::KeyLifetime Lifetime() {
        const auto composed = pdr::http::KeyLifetime::Compose(24);
        EXPECT_TRUE(composed.HasValue());
        return composed.Value();
    }

    RequestSchema Schema() const {
        EXPECT_TRUE(schema.HasValue());
        return schema.Value();
    }

    userver::formats::json::Value Serve(Ask& ask) {
        return userver::formats::json::FromString(handler.Serve(ask));
    }

    Knows callers;
    Decides permissions;
    FakeTenantAwareRepository database;
    FakeIdempotencyKeys keys;
    FakeClock clock;
    core::Result<RequestSchema> schema;
    BookingHandler handler;
};

/// Меняющее обращение с ключом: короче, чем писать это в каждой проверке.
Ask Changing(std::string_view key = kKey, std::string body = kGoodBody) {
    Ask ask;
    ask.Changing(pdr::http::Method::kPost).WithKey(key).WithBody(std::move(body));
    return ask;
}

}  // namespace

/// Полный путь: разобрали, опознали, спросили политику, позвали сценарий.
UTEST(AuthorizedHandler, WalksTheWholeWayAndAsksThePolicyOnTheWay) {
    World world;
    Ask ask;
    ask.WithCookie(std::string{kCookie}, "t.secret");

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 200);
    EXPECT_EQ(world.handler.ran, 1);
    EXPECT_EQ(body["minutes"].As<int>(), 45);
    ASSERT_EQ(world.permissions.asked.size(), 1U);
    EXPECT_EQ(world.permissions.asked.front(), identity::Action::kBookLesson);
    EXPECT_EQ(world.permissions.saw_actor, kActor);
    EXPECT_EQ(world.permissions.saw_subject, kStudent);
}

/// Сценарий работает ВНУТРИ области арендатора: сессия у него та, в которой
/// арендатор уже объявлен базе.
UTEST(AuthorizedHandler, TheScenarioRunsInsideTheTenantScope) {
    World world;
    Ask ask;

    const auto body = world.Serve(ask);

    EXPECT_EQ(body["tenant"].As<std::string>(), kTenant.ToString());
    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 1U);
}

/// Удостоверение берут там, где велел тот, кто его проверяет: имя cookie —
/// знание про сессии, а не про HTTP.
UTEST(AuthorizedHandler, ItLooksWhereTheIdentifierSaidToLook) {
    World world;
    Ask ask;
    ask.WithCookie(std::string{kCookie}, "из cookie")
        .WithHeader(std::string{kAuthorization}, "из заголовка");

    world.Serve(ask);

    EXPECT_EQ(world.callers.saw_cookie, "из cookie");
    EXPECT_EQ(world.callers.saw_header, "из заголовка");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: заголовки безопасности есть на ВСЕХ ответах,
/// включая каждый отказ. Отказ без них — это ровно та страница, которую покажут
/// чужому.
UTEST(AuthorizedHandler, EverySecurityHeaderIsOnEveryAnswerIncludingRefusals) {
    for (int occasion = 0; occasion < 4; ++occasion) {
        World world;
        Ask ask;
        switch (occasion) {
            case 0:
                break;
            case 1:
                ask.WithBody("не json");
                break;
            case 2:
                world.callers.refuse =
                    core::Error{core::ErrorKind::kForbidden, "session_expired", "вышел"};
                break;
            case 3:
                world.permissions.answer = identity::Denied(identity::DenyReason::kNotYours);
                break;
            default:
                break;
        }

        world.Serve(ask);

        for (const auto& header : kSecurityHeaders) {
            const auto& set = ask.GetHttpResponse().headers;
            const auto found = set.find(std::string{header.name});
            ASSERT_NE(found, set.end())
                << "случай " << occasion << ": нет заголовка «" << header.name << "»";
            EXPECT_EQ(found->second, header.value);
        }
    }
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: след запроса возвращается в заголовке И в теле
/// отказа. Человек может назвать только то, что видел сам.
UTEST(AuthorizedHandler, TheRequestIdComesBackInTheHeaderAndInTheRefusal) {
    World world;
    Ask ask;
    ask.WithHeader(std::string{kRequestIdHeader}, "req-0a1b2c3d").WithBody("не json");

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().headers.at(std::string{kRequestIdHeader}), "req-0a1b2c3d")
        << "принесённый след не тот же самый";
    EXPECT_EQ(body["request_id"].As<std::string>(), "req-0a1b2c3d");
}

/// Негодный след клиента не уходит ни в заголовок ответа, ни в тело: иначе
/// строку журнала и лишний заголовок пишет тот, кто прислал запрос.
UTEST(AuthorizedHandler, ADangerousBroughtIdIsReplacedEverywhere) {
    World world;
    Ask ask;
    ask.WithHeader(std::string{kRequestIdHeader}, "req\r\nSet-Cookie: a=b").WithBody("не json");

    const auto body = world.Serve(ask);

    const auto& answered = ask.GetHttpResponse().headers.at(std::string{kRequestIdHeader});
    EXPECT_EQ(answered.find("Set-Cookie"), std::string::npos) << answered;
    EXPECT_TRUE(IsUsableRequestId(answered));
    EXPECT_EQ(body["request_id"].As<std::string>(), answered);
}

UTEST(AuthorizedHandler, WithoutABroughtIdWeStillAnswerWithOne) {
    World world;
    Ask ask;

    world.Serve(ask);

    const auto& set = ask.GetHttpResponse().headers;
    ASSERT_NE(set.find(std::string{kRequestIdHeader}), set.end());
    EXPECT_FALSE(set.at(std::string{kRequestIdHeader}).empty());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: отказ разбора называет ПОЛЕ.
UTEST(AuthorizedHandler, ARefusedBodyNamesTheFieldAndNeverReachesTheScenario) {
    World world;
    Ask ask;
    ask.WithBody(R"({"student_id": "s-1", "starts_at": "x", "minutes": "сорок пять"})");

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 400);
    EXPECT_NE(body["field"].As<std::string>().find("minutes"), std::string::npos);
    EXPECT_EQ(world.handler.ran, 0) << "сценарий позвали с телом, которое не разобралось";
    EXPECT_EQ(ask.GetHttpResponse().headers.at("Content-Type"), kProblemContentType);
}

/// Неопознанный не доходит ни до политики, ни до базы, и слышит 401, а не 403.
UTEST(AuthorizedHandler, AnUnidentifiedCallerGetsFourOhOneAndGoesNoFurther) {
    World world;
    world.callers.refuse = core::Error{core::ErrorKind::kForbidden, "session_expired", "вышел"};
    Ask ask;

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 401);
    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:session_expired");
    EXPECT_TRUE(world.permissions.asked.empty()) << "политику спросили о неизвестно ком";
    EXPECT_EQ(world.handler.ran, 0);
    EXPECT_TRUE(world.database.RowsBypassingPolicy().empty()) << "в базу сходили за неопознанного";
}

/// Отказ политики останавливает запрос ДО сценария и до базы.
UTEST(AuthorizedHandler, ARefusedPolicyStopsBeforeTheScenario) {
    World world;
    world.permissions.answer = identity::Denied(identity::DenyReason::kScopeMissing);
    Ask ask;

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 403);
    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:scope_missing");
    EXPECT_EQ(world.handler.ran, 0);
    EXPECT_TRUE(world.database.RowsBypassingPolicy().empty());
}

/// Чужой кабинет отвечает «не найдено»: 403 подтвердил бы, что ресурс есть.
UTEST(AuthorizedHandler, AForeignTenantIsNotFound) {
    World world;
    world.permissions.answer = identity::Denied(identity::DenyReason::kForeignTenant);
    Ask ask;

    world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 404);
}

/// Отказ сценария приходит по той же таблице, что и всё остальное.
UTEST(AuthorizedHandler, AScenarioRefusalKeepsTheOneShape) {
    World world;
    world.handler.refuse =
        core::Error{core::ErrorKind::kConflict, "slot_already_taken", "это время занято"};
    Ask ask;

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 409);
    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:slot_already_taken");
    EXPECT_EQ(body["detail"].As<std::string>(), "это время занято");
    EXPECT_EQ(body["status"].As<int>(), 409);
    EXPECT_FALSE(body["request_id"].As<std::string>().empty());
}

/// Успешный ответ — не problem+json, и клиент отличает одно от другого не
/// разбирая тело.
UTEST(AuthorizedHandler, AGoodAnswerIsNotAProblem) {
    World world;
    Ask ask;

    world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().headers.at("Content-Type"), "application/json");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: ключ обязателен на всех меняющих обращениях.
/// Отсутствие — отказ, а НЕ «тихо выполнить»: тихо выполнить и есть двойное
/// списание по оборванной связи.
UTEST(Idempotent, EveryChangingRequestNeedsAKeyAndRefusalIsNotSilentSuccess) {
    for (const auto method : {pdr::http::Method::kPost,
                              pdr::http::Method::kPut,
                              pdr::http::Method::kPatch,
                              pdr::http::Method::kDelete}) {
        World world;
        Ask ask;
        ask.Changing(method);

        const auto body = world.Serve(ask);

        EXPECT_EQ(ask.GetHttpResponse().status, 400) << pdr::http::Name(method);
        EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:idempotency_key_required")
            << pdr::http::Name(method);
        EXPECT_EQ(world.handler.ran.load(), 0)
            << pdr::http::Name(method) << ": операция выполнена без ключа";
        EXPECT_TRUE(world.database.RowsBypassingPolicy().empty()) << pdr::http::Name(method);
    }
}

/// Читающему обращению ключ не нужен: требовать его значило бы ломать GET ради
/// защиты от повтора, которого у чтения не бывает.
UTEST(Idempotent, AReadingRequestNeedsNoKey) {
    World world;
    Ask ask;

    world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 200);
    EXPECT_EQ(world.handler.ran.load(), 1);
    EXPECT_EQ(world.keys.Rows(), 0U) << "чтение заняло ключ";
}

/// Негодный ключ — тот же отказ, что и отсутствующий: «1» столкнётся с чужим в
/// первый же день и превратит защиту в отказ постороннему человеку.
UTEST(Idempotent, AKeyTooShortToBeUniqueIsAsGoodAsNoKey) {
    World world;
    Ask ask = Changing("1");

    const auto body = world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 400);
    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:idempotency_key_required");
    EXPECT_EQ(world.handler.ran.load(), 0);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: повтор не создаёт вторую сущность.
UTEST(Idempotent, ARepeatReturnsTheSavedAnswerAndCreatesNothingSecond) {
    World world;
    Ask first = Changing();
    const auto once = world.Serve(first);

    Ask again = Changing();
    const auto twice = world.Serve(again);

    EXPECT_EQ(world.handler.ran.load(), 1) << "операция выполнена дважды";
    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 1U) << "заведена вторая сущность";
    EXPECT_EQ(twice, once) << "повтор ответил не тем, чем ответили в первый раз";
    EXPECT_EQ(again.GetHttpResponse().status, 200);
    EXPECT_EQ(again.GetHttpResponse().headers.at(std::string{kReplayedHeader}), "true");
    EXPECT_EQ(first.GetHttpResponse().headers.count(std::string{kReplayedHeader}), 0U)
        << "первый ответ помечен сохранённым";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: тот же ключ с другим телом — 409. Это ошибка
/// клиента, а не повтор, и отвечать на неё сохранённым ответом нельзя: он про
/// другой запрос.
UTEST(Idempotent, TheSameKeyWithAChangedBodyIsAClientMistake) {
    World world;
    Ask first = Changing();
    world.Serve(first);

    Ask other = Changing(kKey, kOtherBody);
    const auto body = world.Serve(other);

    EXPECT_EQ(other.GetHttpResponse().status, 409);
    EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:idempotency_key_reused");
    EXPECT_EQ(world.handler.ran.load(), 1) << "операция выполнена по чужому ключу";
    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 1U);
}

UTEST(Idempotent, ADifferentKeyIsADifferentRequest) {
    World world;
    Ask first = Changing("idempotency-first-11");
    Ask second = Changing("idempotency-second-2");

    world.Serve(first);
    world.Serve(second);

    EXPECT_EQ(world.handler.ran.load(), 2);
    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 2U);
}

/// ГЛАВНЫЙ ТЕСТ ЗАДАЧИ: два одинаковых обращения ОДНОВРЕМЕННО, в разных
/// сопрограммах. Сущность обязана появиться ровно одна.
///
/// Мьютекс в процессе для этого не годится и запрещён: реплик бывает больше
/// одной, и мьютекс одной про другую не знает. Здесь его нет — атомарность даёт
/// хранилище ключей, как в базе её даёт первичный ключ.
UTEST_MT(Idempotent, TwoAtOnceCreateExactlyOneThing, 2) {
    World world;
    world.handler.takes = std::chrono::milliseconds{50};

    Ask left = Changing();
    Ask right = Changing();

    auto first = userver::engine::AsyncNoSpan([&] { return world.handler.Serve(left); });
    auto second = userver::engine::AsyncNoSpan([&] { return world.handler.Serve(right); });
    first.Get();
    second.Get();

    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 1U)
        << "одновременный повтор завёл вторую сущность — ровно то, ради чего всё написано";
    EXPECT_EQ(world.keys.Taken(), 1) << "ключ заняли дважды";
    EXPECT_EQ(world.handler.ran.load(), 1) << "операция выполнена параллельно сама с собой";

    const std::set<int> answered{left.GetHttpResponse().status, right.GetHttpResponse().status};
    EXPECT_EQ(answered, (std::set<int>{200, 409}))
        << "ответы «" << left.GetHttpResponse().status << "» и «" << right.GetHttpResponse().status
        << "»: второму обязаны сказать повторить";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: отказ операции не оставляет несогласованного
/// состояния. Ключ и операция — одна область, поэтому откатывается либо всё,
/// либо ничего, и повтор начинает с чистого места.
UTEST(Idempotent, AFailedOperationLeavesNeitherKeyNorEntityBehind) {
    World world;
    world.handler.refuse =
        core::Error{core::ErrorKind::kConflict, "slot_already_taken", "это время занято"};

    Ask failing = Changing();
    const auto refused = world.Serve(failing);

    EXPECT_EQ(failing.GetHttpResponse().status, 409);
    EXPECT_EQ(refused["type"].As<std::string>(), "urn:pdr:error:slot_already_taken");
    EXPECT_TRUE(world.database.RowsBypassingPolicy().empty());

    world.handler.refuse.reset();
    Ask retried = Changing();
    world.Serve(retried);

    EXPECT_EQ(retried.GetHttpResponse().status, 200)
        << "отказ залип на ключе: человек не может повторить, даже когда слот освободился";
    EXPECT_EQ(world.database.RowsBypassingPolicy().size(), 1U);
}

/// Ключ занимается ПОСЛЕ политики: неопознанный и тот, кому нельзя, ключей не
/// занимают вовсе — иначе чужой ключ можно занять, не имея права ни на что.
UTEST(Idempotent, ARefusedRequestTakesNoKey) {
    World world;
    world.permissions.answer = identity::Denied(identity::DenyReason::kNotYours);

    Ask ask = Changing();
    world.Serve(ask);

    EXPECT_EQ(ask.GetHttpResponse().status, 403);
    EXPECT_EQ(world.keys.Rows(), 0U) << "отказ политики занял ключ";
}

}  // namespace pdr::infrastructure::http
