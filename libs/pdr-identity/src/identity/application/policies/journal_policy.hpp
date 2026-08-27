#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Право смотреть журнал доступа к своим данным.
///
/// УЧЕНИК ВИДИТ, КТО К НЕМУ ЗАХОДИЛ, — и это настоящая гарантия, а не абзац в
/// политике конфиденциальности. Опекун может иметь доступ к записи занятия и
/// всё равно оставить след; след виден тому, о ком он.
///
/// ОПЕКУНА ЗДЕСЬ НЕТ НИ ПРИ КАКОМ УРОВНЕ, и это главное решение файла. Журнал
/// заведён затем, чтобы ученик видел, кто его смотрел; опекун, читающий этот
/// журнал, видит, заметил ли ребёнок его просмотры, — и гарантия перестаёт
/// быть гарантией.
///
/// Репетитор видит журнал своего ученика: он отвечает за занятие, и «кто-то
/// посторонний открывал запись урока» — сведение для него.
class JournalPolicy final : public Policy {
public:
    JournalPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
