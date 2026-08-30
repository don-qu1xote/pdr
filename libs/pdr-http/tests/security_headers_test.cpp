#include "infrastructure/http/security_headers.hpp"

#include <map>
#include <set>
#include <string>

#include <userver/utest/utest.hpp>

namespace pdr::infrastructure::http {
namespace {

/// Двойник ответа: ровно тот метод, которым пользуется `ApplySecurityHeaders`.
class Answering final {
public:
    void SetHeader(std::string name, std::string value) {
        set[std::move(name)] = std::move(value);
    }

    std::map<std::string, std::string> set;
};

}  // namespace

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: все четыре заголовка стоят.
UTEST(SecurityHeaders, AllFourAreSet) {
    Answering answer;
    ApplySecurityHeaders(answer);

    for (const auto* name : {"Content-Security-Policy",
                             "Referrer-Policy",
                             "Permissions-Policy",
                             "X-Content-Type-Options"}) {
        const auto found = answer.set.find(name);
        ASSERT_NE(found, answer.set.end()) << "нет заголовка «" << name << "»";
        EXPECT_FALSE(found->second.empty()) << name << ": заголовок пустой";
    }
    EXPECT_EQ(answer.set.size(), kSecurityHeaders.size());
}

UTEST(SecurityHeaders, NoneIsNamedTwice) {
    std::set<std::string_view> seen;
    for (const auto& header : kSecurityHeaders) {
        EXPECT_TRUE(seen.insert(header.name).second)
            << header.name << ": имя названо дважды, и второе значение перебьёт первое";
    }
}

/// У API нет ни одного законного повода что-нибудь подгрузить, а незаконный
/// есть: отражённый ответ, который браузер решит показать.
UTEST(SecurityHeaders, TheApiLoadsNothingAndEmbedsNowhere) {
    Answering answer;
    ApplySecurityHeaders(answer);

    const auto& policy = answer.set.at("Content-Security-Policy");
    EXPECT_NE(policy.find("default-src 'none'"), std::string::npos);
    EXPECT_NE(policy.find("frame-ancestors 'none'"), std::string::npos);
    EXPECT_EQ(answer.set.at("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(answer.set.at("Referrer-Policy"), "no-referrer");
}

}  // namespace pdr::infrastructure::http
