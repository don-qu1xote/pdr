#pragma once

#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "scheduling/core/lesson.hpp"

/// @file
/// Общая почва проверок расписания: арендатор, репетитор, ученик, зона и
/// занятие-образец.
///
/// Отдельным заголовком, а не внутри contract-набора: тем же значениям нужны
/// живые проверки, которые сам набор не включают (гонка и план запроса), а
/// включить его им нечем — набор разворачивается в UTEST-макросы ровно один раз
/// на двоичный файл.
namespace pdr::scheduling::testing {

/// Значения, одинаковые у всех миров набора. Собраны здесь, чтобы мир отвечал
/// только за «как создать реализацию», а не за содержимое проверок.
struct ContractGround final {
    static core::TenantId Tenant();
    static core::PersonId Tutor();
    static core::PersonId Student();
    static core::TimeZone Zone();
    static core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour);
    static core::TimeRange Window(core::Instant from, core::Instant to);
    static Lesson ALesson(core::LessonId id, core::Instant starts_at);
};

}  // namespace pdr::scheduling::testing
