#pragma once

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "core/types/ids.hpp"

namespace pdr::application::ports {

/// Хранилище, у которого не бывает запросов «вообще»: каждый идёт от имени
/// арендатора.
///
/// Изоляция сделана в базе политикой RLS (ADR-0003): строка чужого арендатора
/// не видна ни одному запросу, включая `select` без `where`. Политике нужно
/// знать, от чьего имени идёт запрос, и это знание — часть контура подключения,
/// а не аргумент вызова: параметр сессии `pdr.tenant_id`.
///
/// Порт существует ровно затем, чтобы «забыть объявить арендатора» было НЕЧЕМ.
/// Сессия не создаётся снаружи и не живёт дольше области: единственный способ
/// её получить — попросить область здесь, а область объявляет арендатора до
/// первого запроса и заканчивается вместе с транзакцией.
///
/// Даже если объявление всё-таки не случится, чужого никто не увидит: политика
/// сравнит `tenant_id` с NULL, и не подойдёт ни одна строка. Забытый параметр —
/// это пустой ответ, а не чужие данные. Пустой ответ замечают в тот же день;
/// чужие данные в ответе не замечает никто.
///
/// Порт узкий намеренно. Выборок и сохранений здесь нет — это вопросы
/// контекстов, у каждого свой порт (`identity::ports::GuardianshipRepository`).
/// Общего Repository с двадцатью методами в проекте не будет: он заставляет
/// каждый фейк реализовывать девятнадцать лишних.
///
/// Тип сессии — параметр шаблона: слой application не имеет права знать ни про
/// userver, ни про Postgres, а адаптер связывает порт со своей транзакцией.
template<class Session>
class TenantAwareRepository {
public:
    using Work = std::function<void(Session&)>;

    TenantAwareRepository(const TenantAwareRepository&) = delete;
    TenantAwareRepository& operator=(const TenantAwareRepository&) = delete;

    virtual ~TenantAwareRepository() = default;

    /// Провести единицу работы от имени арендатора и вернуть её результат.
    ///
    /// Обёртка не виртуальная: виртуальных шаблонов не бывает, а возвращать
    /// сценарии должны что угодно. Тот же приём, что у `IdGenerator::Next<Id>()`.
    template<class Job>
    auto InTenant(const core::TenantId& tenant, Job&& job) {
        using Result = std::invoke_result_t<Job, Session&>;
        static_assert(!std::is_reference_v<Result>,
                      "Работа не возвращает ссылку: сессия и её строки живут только "
                      "внутри области, а ссылка на них пережила бы транзакцию");

        if constexpr (std::is_void_v<Result>) {
            Run(tenant, Work{[&job](Session& session) { job(session); }});
        } else {
            std::optional<Result> result;
            Run(tenant, Work{[&job, &result](Session& session) { result.emplace(job(session)); }});
            return std::move(*result);
        }
    }

protected:
    TenantAwareRepository() = default;

    /// Реализация обязана: объявить арендатора базе ДО первого запроса работы,
    /// выполнить работу в той же транзакции и снять объявление вместе с ней.
    virtual void Run(const core::TenantId& tenant, const Work& work) = 0;
};

}  // namespace pdr::application::ports
