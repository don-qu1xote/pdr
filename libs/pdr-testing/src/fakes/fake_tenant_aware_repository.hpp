#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"

namespace pdr::testing {

class FakeTenantAwareRepository;

/// Строка фейкового хранилища: арендатор и то, что в ней лежит.
struct FakeRow {
    core::TenantId tenant;
    std::string payload;
};

/// Сессия внутри области. Ведёт себя как база с политикой, а не как «список,
/// который тест не забыл отфильтровать»: показывает и трогает строки только
/// объявленного арендатора.
///
/// Фейк, у которого политики нет, делает зелёными ровно те тесты, которые в
/// проде окажутся утечкой, — поэтому она здесь.
class FakeTenantSession final {
public:
    FakeTenantSession(const FakeTenantSession&) = delete;
    FakeTenantSession& operator=(const FakeTenantSession&) = delete;

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }

    /// Всё, что видно из области: `select` без `where`. Главный вопрос задачи.
    std::vector<std::string> SelectAll() const;

    /// Вставка от имени арендатора области.
    void Insert(std::string payload);

    /// Вставка с чужим арендатором: отказ, как `with check` в базе. Возвращает
    /// false, и хранилище остаётся прежним.
    bool InsertFor(const core::TenantId& owner, std::string payload);

    /// `delete` без `where`: сколько строк исчезло.
    std::size_t DeleteAll();

    /// Что откатить, если из области вышли исключением.
    ///
    /// Настоящая область откатывается сама: не позвали `Commit` — транзакция
    /// ушла назад целиком, вместе со всем, что в ней успели написать ДРУГИЕ
    /// адаптеры. Фейк без этого зеленил бы ровно ту проверку, ради которой всё
    /// написано: строка, оставшаяся после отказа, в проде не остаётся.
    void OnRollback(std::function<void()> undo);

private:
    friend class FakeTenantAwareRepository;

    /// Откатить всё, о чём просили, в обратном порядке.
    void Unwind();

    /// Закрыт намеренно: сессия появляется только внутри области. Отрицательный
    /// тест core.compile_fail.tenant_session_outside_scope стережёт это.
    FakeTenantSession(std::vector<FakeRow>& rows, core::TenantId tenant) noexcept;

    std::vector<FakeRow>& rows_;
    core::TenantId tenant_;
    std::vector<std::function<void()>> undo_;
};

/// Единственный фейк порта арендатора. Свой в каждом тесте означал бы пять
/// разных представлений о том, что видно из области.
class FakeTenantAwareRepository final
    : public application::ports::TenantAwareRepository<FakeTenantSession> {
public:
    FakeTenantAwareRepository() = default;

    /// Все строки, минуя политику. Только для проверок самого теста: так
    /// «под другим арендатором не видно» отличается от «строки нет вовсе».
    const std::vector<FakeRow>& RowsBypassingPolicy() const noexcept {
        return rows_;
    }

    /// Сколько раз область объявляла арендатора. Считается ДО работы, поэтому
    /// внутри области значение уже увеличено — на этом и держится проверка
    /// «объявили раньше, чем спросили».
    int Declarations() const noexcept {
        return declarations_;
    }

private:
    void Run(const core::TenantId& tenant, const Work& work) override;

    std::vector<FakeRow> rows_;
    int declarations_{0};
};

}  // namespace pdr::testing
