#pragma once

#include "core/errors.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/job_settings.hpp"

namespace pdr::jobs::ports {

/// Откуда берутся имя блокировки, период и отведённое на прогон время.
///
/// Умолчаний нет ни у одного значения. Задания, которого нет в конфиге, для
/// механизма не существует: это отказ на старте, а не «возьмём минуту и
/// поедем». Значение по умолчанию у периода означало бы, что забытая настройка
/// выглядит как работающее задание.
class JobSettingsSource {
public:
    JobSettingsSource(const JobSettingsSource&) = delete;
    JobSettingsSource& operator=(const JobSettingsSource&) = delete;

    virtual ~JobSettingsSource() = default;

    virtual core::Result<JobSettings> For(const JobName& job) const = 0;

protected:
    JobSettingsSource() = default;
};

}  // namespace pdr::jobs::ports
