#pragma once

#include "core/types/ids.hpp"

namespace pdr::application::ports {

/// Генератор идентификаторов — тоже порт: в тесте нужна предсказуемая
/// последовательность, а не случайные шестнадцать байт, иначе ожидаемый
/// результат нечем записать.
///
/// Виртуальный метод ровно один. Метод на каждый тип идентификатора означал бы
/// переписывание каждого фейка при появлении новой сущности, а виртуальных
/// шаблонов не бывает — поэтому тип навешивается снаружи, шаблонной обёрткой:
///
///     const auto lesson = generator.Next<core::LessonId>();
class IdGenerator {
public:
    IdGenerator(const IdGenerator&) = delete;
    IdGenerator& operator=(const IdGenerator&) = delete;

    virtual ~IdGenerator() = default;

    template<class Id>
    Id Next() const {
        static_assert(core::kIsStrongId<Id>,
                      "Next() выдаёт только типизированные идентификаторы: Next<core::PersonId>()");
        return Id::FromBytes(NextBytes());
    }

protected:
    IdGenerator() = default;

    virtual core::IdBytes NextBytes() const = 0;
};

}  // namespace pdr::application::ports
