#pragma once

#include <string_view>

#include "core/errors.hpp"
#include "identity/contract.hpp"
#include "infrastructure/http/problem.hpp"

namespace pdr::infrastructure::http {

/// Куда приходит запрос и с каким следом. Оба значения одинаковы для всех
/// отказов одного запроса, поэтому едут вместе, а не двумя параметрами подряд,
/// которые однажды переставят местами.
struct Occasion final {
    std::string_view instance;
    std::string_view request_id;
};

/// ЕДИНСТВЕННОЕ МЕСТО, где доменный отказ превращается в ответ HTTP.
///
/// Доменная ошибка про HTTP не знает ничего и знать не должна: `core::Error`
/// живёт в ядре, а ядро не собирается ни с userver, ни с сетью. Обратное —
/// каждый хендлер сам решает, что такое 409, — заканчивается тем, что «слот
/// занят» приходит то 400, то 409, то 500, и клиент разбирает три случая
/// вместо одного.
///
/// Разбор идёт ПО РОДУ, а не по коду. Кодов сотня и будет больше, и таблица
/// «код → статус» разошлась бы с доменом на первой же задаче. Род закрыт
/// списком, обход `kEveryErrorKind` полон, и статус есть у каждого.
int StatusOf(core::ErrorKind kind) noexcept;

std::string_view TitleOf(core::ErrorKind kind) noexcept;

Problem AsProblem(const core::Error& error, const Occasion& occasion);

/// Отказ политики. Своя таблица, потому что это ДРУГОЙ вопрос: домен отвечает
/// «так нельзя», политика — «вам нельзя», и человеку это разные новости.
///
/// `kForeignTenant` отдаёт 404, а не 403, намеренно: 403 подтвердил бы, что
/// ресурс существует, — и подбор чужих идентификаторов стал бы способом узнать
/// про чужой кабинет.
///
/// `kNoPolicy` отдаёт 500: действие без политики — не отказ человеку, а наша
/// поломка настройки, и в четырёхсотых её никто не заметит.
int StatusOf(identity::DenyReason reason) noexcept;

std::string_view TitleOf(identity::DenyReason reason) noexcept;

Problem AsProblem(const identity::PolicyDecision& decision, const Occasion& occasion);

/// «Кто ты» — не «тебе нельзя», и ответ у них разный: 401 говорит клиенту
/// войти заново, 403 — что входить бесполезно. Родом доменной ошибки это не
/// выражается: сессия отвечает `kNotFound` и `kForbidden`, а по ним 401 не
/// собрать. Поэтому шаг опознания зовёт эту функцию, и зовёт её ровно он один.
Problem Unidentified(const core::Error& error, const Occasion& occasion);

/// Тело запроса не разобралось: не JSON или не по схеме. 400, а не 422 —
/// до правил домена дело не дошло вовсе.
Problem Malformed(const core::Error& error, std::string_view field, const Occasion& occasion);

}  // namespace pdr::infrastructure::http
