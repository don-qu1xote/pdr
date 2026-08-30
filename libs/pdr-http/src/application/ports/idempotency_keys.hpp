#pragma once

#include <string>

#include "core/errors.hpp"
#include "core/idempotency.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::http::ports {

/// Хранилище ключей идемпотентности.
///
/// ОБА МЕТОДА БЕРУТ СЕССИЮ ПАРАМЕТРОМ, и это главное в этом порту. Занять ключ
/// и выполнить операцию обязано быть ОДНОЙ транзакцией: запись ключа отдельной
/// транзакцией даёт ровно ту дырку, ради закрытия которой всё написано — упали
/// между ними, и либо операция прошла без ключа (повтор выполнит её второй раз),
/// либо ключ занят без операции (повтор не выполнит её никогда).
///
/// Тип сессии — параметр шаблона по той же причине, что у
/// `application::ports::TenantAwareRepository`: слой application не имеет права
/// знать ни про userver, ни про Postgres.
///
/// Порт узкий до двух методов: занять и завершить. Ни «посмотреть», ни
/// «удалить» здесь нет — первое незачем, второе делает уборка под своей ролью.
template<class Session>
class IdempotencyKeys {
public:
    IdempotencyKeys(const IdempotencyKeys&) = delete;
    IdempotencyKeys& operator=(const IdempotencyKeys&) = delete;

    virtual ~IdempotencyKeys() = default;

    /// Занять ключ под этот запрос.
    ///
    /// Реализация обязана быть АТОМАРНОЙ: два обращения с одним ключом,
    /// пришедшие одновременно на разные реплики, получают `kTaken` и что-то
    /// другое, а не два `kTaken`. В базе это `insert ... on conflict do
    /// nothing` и первичный ключ, а не «сначала select, потом insert»:
    /// мьютекса в процессе тут не хватит — реплик бывает больше одной.
    ///
    /// Отпечаток не совпал с записанным — отказ `idempotency_key_reused`: тот
    /// же ключ с другим телом это ошибка клиента, а не повтор.
    virtual core::Result<Claim> Take(Session& session,
                                     const core::TenantId& tenant,
                                     const IdempotencyKey& key,
                                     const RequestFingerprint& fingerprint,
                                     core::Instant expires_at) = 0;

    /// Записать ответ и закрыть ключ. Зовётся в той же транзакции, в которой
    /// прошла операция, и только после её успеха.
    virtual core::Result<void> Complete(Session& session,
                                        const core::TenantId& tenant,
                                        const IdempotencyKey& key,
                                        const SavedAnswer& answer) = 0;

protected:
    IdempotencyKeys() = default;
};

}  // namespace pdr::http::ports
