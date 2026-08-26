#include "identity/infrastructure/http/session_transport.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "builders/auth_world.hpp"
#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "identity/application/authenticate_session.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

/// Запрос ровно с теми двумя вопросами, которые задаёт чтение транспорта.
/// Своего интерфейса поверх userver для этого не заводили: у настоящего
/// `server::http::HttpRequest` те же `GetCookie` и `GetHeader`, и двойник — это
/// тип с теми же именами, а не наследник чего-то придуманного.
class RequestDouble final {
public:
    void SetCookie(std::string name, std::string value) {
        cookies_.insert_or_assign(std::move(name), std::move(value));
    }
    void SetHeader(std::string name, std::string value) {
        headers_.insert_or_assign(std::move(name), std::move(value));
    }

    const std::string& GetCookie(const std::string& name) const {
        return Look(cookies_, name);
    }
    const std::string& GetHeader(const std::string& name) const {
        return Look(headers_, name);
    }

private:
    using Bag = std::unordered_map<std::string, std::string>;

    const std::string& Look(const Bag& bag, const std::string& name) const {
        const auto found = bag.find(name);
        return found == bag.end() ? nothing_ : found->second;
    }

    Bag cookies_;
    Bag headers_;
    std::string nothing_;
};

class SessionTransportTest : public ::testing::Test {
protected:
    SessionTransportTest()
        : id_{Numbered<core::TenantId>(1), Numbered<SessionSecret>(7)},
          seen_{digests_.Of("Chrome/1.0"), digests_.Of("192.0.2.10")} {
        sessions_.Save(Session::Open(id_,
                                     Numbered<core::PersonId>(20),
                                     clock_.Now(),
                                     std::chrono::duration_cast<core::Instant::Duration>(
                                         std::chrono::hours{24}),
                                     seen_)
                           .Value());
    }

    testing::FakeDigests digests_;
    testing::FakeSessions sessions_;
    pdr::testing::FakeClock clock_;
    SessionId id_;
    Fingerprint seen_;
};

TEST_F(SessionTransportTest, TextFormRoundTrips) {
    const auto parsed = SessionId::Parse(id_.ToString());

    ASSERT_TRUE(parsed.HasValue());
    EXPECT_EQ(parsed.Value(), id_);
    EXPECT_EQ(parsed.Value().Tenant(), id_.Tenant());
    EXPECT_EQ(parsed.Value().Secret(), id_.Secret());
}

TEST_F(SessionTransportTest, GarbageIsNotGuessed) {
    EXPECT_FALSE(SessionId::Parse("").HasValue());
    EXPECT_FALSE(SessionId::Parse("без-точки").HasValue());
    EXPECT_FALSE(SessionId::Parse(id_.Secret().ToString()).HasValue());
    EXPECT_FALSE(SessionId::Parse("не-арендатор." + id_.Secret().ToString()).HasValue());
}

TEST_F(SessionTransportTest, CookieCarriesTheIdentifier) {
    RequestDouble request;
    request.SetCookie(std::string{http::transport::kCookie}, id_.ToString());

    const auto read = http::ReadSessionId(request);

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, id_);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: проверка сессии не зависит от транспорта.
///
/// Один и тот же идентификатор приезжает заголовком вместо cookie — и проверка
/// отвечает ровно то же самое. Ни строки различия: `AuthenticateSession` про
/// транспорт не знает вовсе.
TEST_F(SessionTransportTest, HeaderInsteadOfCookieChangesNothing) {
    RequestDouble by_cookie;
    by_cookie.SetCookie(std::string{http::transport::kCookie}, id_.ToString());

    RequestDouble by_header;
    by_header.SetHeader(std::string{http::transport::kHeader},
                        std::string{http::transport::kBearer} + id_.ToString());

    const auto from_cookie = http::ReadSessionId(by_cookie);
    const auto from_header = http::ReadSessionId(by_header);

    ASSERT_TRUE(from_cookie.has_value());
    ASSERT_TRUE(from_header.has_value());
    EXPECT_EQ(*from_cookie, *from_header);

    const AuthenticateSession check{sessions_, clock_};
    const auto cookie_says = check.Execute(*from_cookie);
    const auto header_says = check.Execute(*from_header);

    ASSERT_TRUE(cookie_says.HasValue());
    ASSERT_TRUE(header_says.HasValue());
    EXPECT_EQ(cookie_says.Value().Person(), header_says.Value().Person());
    EXPECT_EQ(cookie_says.Value().Id(), header_says.Value().Id());
}

/// Порядок источников закрыт: cookie перебивает заголовок. Иначе чужой сайт,
/// подставивший заголовок, работал бы поверх cookie собственного.
TEST_F(SessionTransportTest, CookieWinsOverHeader) {
    const SessionId other{Numbered<core::TenantId>(1), Numbered<SessionSecret>(8)};

    RequestDouble request;
    request.SetCookie(std::string{http::transport::kCookie}, id_.ToString());
    request.SetHeader(std::string{http::transport::kHeader},
                      std::string{http::transport::kBearer} + other.ToString());

    const auto read = http::ReadSessionId(request);

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, id_);
}

TEST_F(SessionTransportTest, HeaderWithoutBearerIsNotRead) {
    RequestDouble request;
    request.SetHeader(std::string{http::transport::kHeader}, id_.ToString());

    EXPECT_FALSE(http::ReadSessionId(request).has_value());
}

TEST_F(SessionTransportTest, NothingBroughtIsNothingRead) {
    EXPECT_FALSE(http::ReadSessionId(RequestDouble{}).has_value());
}

/// Атрибуты cookie — не стиль, а три разные защиты. Проверка стоит здесь,
/// чтобы «убрали Secure на время отладки» роняло сборку, а не выяснялось потом.
TEST(SessionCookie, CarriesAllThreeGuards) {
    const std::string attributes{http::transport::kCookieAttributes};

    EXPECT_NE(attributes.find("HttpOnly"), std::string::npos);
    EXPECT_NE(attributes.find("Secure"), std::string::npos);
    EXPECT_NE(attributes.find("SameSite=Lax"), std::string::npos);
    EXPECT_EQ(http::transport::kCookie.rfind("__Host-", 0), 0U)
        << "приставка __Host- запрещает поддомену подсунуть свою cookie основному";
}

}  // namespace
}  // namespace pdr::identity
