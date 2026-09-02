#pragma once

/// @file
/// СОПРЯЖЕНИЕ ПОРОЖДЁННОЙ ЗАПИСИ С ДОМЕННЫМ ТИПОМ.
///
/// Путь файла задан не нами: штатное порождение (`x-usrv-cpp-type` в схеме
/// величины) ищет заголовок ровно по `userver/chaotic/io/<путь типа>.hpp`.
///
/// ЧТО ЗДЕСЬ ПРОИСХОДИТ И ЗАЧЕМ. Схема задаёт пределы КАЖДОГО поля по
/// отдельности, и их проверяет порождённый разборщик. Связь между полями схемой
/// не выражается: «прогону отведено меньше периода» и «молчание длиннее периода»
/// — это два поля сразу. Такие правила остаются доменными и живут в
/// `JobSettings::Compose`, а сюда приходит вызов.
///
/// ИСКЛЮЧЕНИЕ ЗДЕСЬ — СПОСОБ ОТВЕРГНУТЬ ЗАПИСЬ ЦЕЛИКОМ, а не сигнал об ошибке.
/// Штатный `chaotic::WithType` ловит его и превращает в отказ разбора всей
/// величины, поэтому негодная запись не применяется вовсе — продолжает
/// действовать прежняя. Вернуть вместо этого «настройки по умолчанию» значило бы
/// молча запустить задание не с тем периодом.
///
/// Шаблоны, а не обычные функции: порождённая запись объявляется НИЖЕ по тексту
/// (этот заголовок включается порождённым файлом до неё), и назвать её тип здесь
/// нечем. Полям она соответствует по построению — они из той же схемы.

#include <chrono>
#include <stdexcept>
#include <string>

#include <userver/chaotic/convert/to.hpp>

#include "jobs/core/job_name.hpp"
#include "jobs/core/job_settings.hpp"

namespace pdr::jobs {

/// Сравнение настроек. Нужно ПОРОЖДЁННОЙ структуре: величина — словарь заданий,
/// а у словаря есть равенство, и оно требует равенства значений.
///
/// Объявлено здесь, а не в домене: в `core` этой надобности нет — она пришла от
/// порождения, и жить ей там, откуда пришла. Сравнением полей, а не адресов:
/// настройки — значение, два одинаковых набора чисел это одно и то же задание.
inline bool operator==(const JobSettings& left, const JobSettings& right) {
    return left.Lock().Value() == right.Lock().Value() && left.Period() == right.Period() &&
           left.Attempt() == right.Attempt() && left.SilenceAllowed() == right.SilenceAllowed() &&
           left.Enabled() == right.Enabled();
}

template<typename Raw>
JobSettings Convert(Raw&& raw, USERVER_NAMESPACE::chaotic::convert::To<JobSettings>) {
    const auto lock = JobName::Parse(raw.lock);
    if (!lock.has_value()) {
        throw std::runtime_error{"имя блокировки не по правилу: " + std::string{raw.lock}};
    }

    const auto milliseconds = [](int value) {
        return std::chrono::duration_cast<JobSettings::Duration>(std::chrono::milliseconds{value});
    };

    auto settings = JobSettings::Compose(*lock,
                                         milliseconds(raw.period_ms),
                                         milliseconds(raw.attempt_ms),
                                         milliseconds(raw.silence_allowed_ms),
                                         raw.enabled);
    if (!settings.HasValue()) {
        throw std::runtime_error{"настройки задания не сходятся: " + settings.Failure().Code() +
                                 " — " + settings.Failure().Detail()};
    }
    return settings.Value();
}

template<typename Raw>
Raw Convert(const JobSettings& settings, USERVER_NAMESPACE::chaotic::convert::To<Raw>) {
    const auto milliseconds = [](JobSettings::Duration value) {
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(value).count());
    };

    Raw raw{};
    raw.lock = settings.Lock().Value();
    raw.period_ms = milliseconds(settings.Period());
    raw.attempt_ms = milliseconds(settings.Attempt());
    raw.silence_allowed_ms = milliseconds(settings.SilenceAllowed());
    raw.enabled = settings.Enabled();
    return raw;
}

}  // namespace pdr::jobs
