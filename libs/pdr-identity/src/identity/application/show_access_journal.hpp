#pragma once

#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/application/ports/access_journal.hpp"
#include "identity/contract.hpp"

namespace pdr::identity {

/// Показать журнал доступа к данным человека.
///
/// УЧЕНИК ВИДИТ, КТО К НЕМУ ЗАХОДИЛ, — и это настоящая гарантия родительского
/// доступа, а не абзац в политике конфиденциальности. Право смотреть и право
/// смотреть незаметно — разные вещи; журнал закрывает вторую.
///
/// Права спрашиваются у контракта, а не проверяются здесь условиями: сценарий,
/// который решает права сам, — это первый из двадцати, каждый со своим
/// пониманием правила.
///
/// В журнале и отказы: «кто-то пытался открыть твою запись занятия, и ему не
/// дали» — сведение, которое человеку нужнее списка удачных просмотров.
///
/// Отказ несёт КОД-ЛИТЕРАЛ `journal_not_yours`, а не имя причины: код, собранный
/// в рантайме, — это код, которому нельзя написать русский текст, потому что
/// проверка речи ищет коды в дереве (`scripts/check-copy.mjs`). Причина уходит в
/// подробность — для журнала и разбора жалобы, — а отображение `DenyReason` в
/// problem+json остаётся одним долгом в одном месте
/// (docs/architecture/first-service.md).
class ShowAccessJournal final {
public:
    ShowAccessJournal(const Contract& permissions, const ports::AccessJournal& journal) noexcept;

    core::Result<std::vector<AccessRecord>> Execute(const core::TenantId& tenant,
                                                    const core::PersonId& actor,
                                                    const Resource& about,
                                                    core::Instant since) const;

private:
    const Contract& permissions_;
    const ports::AccessJournal& journal_;
};

}  // namespace pdr::identity
