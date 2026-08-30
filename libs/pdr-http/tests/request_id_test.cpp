#include "infrastructure/http/request_id.hpp"

#include <string>

#include <userver/tracing/span.hpp>
#include <userver/utest/utest.hpp>

namespace pdr::infrastructure::http {
namespace {

/// Двойник запроса: ровно тот метод, которым пользуется `RequestIdOf`.
class Asking final {
public:
    explicit Asking(std::string brought) : brought_{std::move(brought)} {}

    const std::string& GetHeader(const std::string& name) const {
        return name == std::string{kRequestIdHeader} ? brought_ : nothing_;
    }

private:
    std::string brought_;
    const std::string nothing_;
};

}  // namespace

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: принесённый след возвращается неизменным.
UTEST(RequestId, WhatTheClientBroughtComesBackUntouched) {
    const Asking asking{"6f1b3c-client-side"};

    EXPECT_EQ(RequestIdOf(asking), "6f1b3c-client-side");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: не принёс — след всё равно есть.
UTEST(RequestId, WithoutOneWeMakeOurOwn) {
    const Asking silent{""};

    const auto made = RequestIdOf(silent);

    EXPECT_FALSE(made.empty()) << "запрос без следа: жалобу по нему не разобрать";
    EXPECT_TRUE(IsUsableRequestId(made)) << "свой след не проходит собственную проверку";
}

/// Свой след — ссылка трассировки, то есть ТО ЖЕ значение, что userver пишет в
/// каждую строку журнала. Второй идентификатор рядом означал бы, что человек
/// называет один, а в логах лежит другой.
UTEST(RequestId, OurOwnIsTheOneTheLogsAlreadyCarry) {
    const Asking silent{""};
    userver::tracing::Span span{"request"};

    EXPECT_EQ(RequestIdOf(silent), span.GetLink());
}

/// СЛЕД ПРИХОДИТ СНАРУЖИ, а уходит в журнал и в заголовок ответа. Перевод
/// строки в нём — это строка журнала, написанная клиентом, и лишний заголовок
/// ответа, написанный им же.
UTEST(RequestId, ADangerousOneIsNotTakenAtAll) {
    for (const auto* brought :
         {"пере\nвод", "с пробелом", "c\rвозвратом", "точка;с;запятой", "\x01управляющий"}) {
        const Asking asking{brought};

        EXPECT_FALSE(IsUsableRequestId(brought)) << brought;
        EXPECT_NE(RequestIdOf(asking), brought)
            << "клиент дописал в журнал и в ответ что хотел: " << brought;
    }
}

UTEST(RequestId, TooLongIsNotTakenEither) {
    const std::string endless(kRequestIdLimit + 1, 'a');
    const Asking asking{endless};

    EXPECT_FALSE(IsUsableRequestId(endless));
    EXPECT_NE(RequestIdOf(asking), endless);
    EXPECT_TRUE(IsUsableRequestId(std::string(kRequestIdLimit, 'a')));
}

}  // namespace pdr::infrastructure::http
