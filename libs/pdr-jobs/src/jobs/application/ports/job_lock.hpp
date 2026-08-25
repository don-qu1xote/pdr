#pragma once

namespace pdr::jobs::ports {

/// Блокировка, под которой идёт прогон.
///
/// Механизм её не берёт и не отпускает — это дело штатного
/// `storages::postgres::DistLock`. Отсюда задаётся ровно один вопрос: она ещё
/// наша? Спрашивать приходится посреди работы, между единицами плана: воркер,
/// у которого блокировку отобрали, обязан остановиться сам, иначе двое работают
/// одновременно, каждый в уверенности, что он один.
class JobLock {
public:
    JobLock(const JobLock&) = delete;
    JobLock& operator=(const JobLock&) = delete;

    virtual ~JobLock() = default;

    virtual bool IsHeld() const = 0;

protected:
    JobLock() = default;
};

}  // namespace pdr::jobs::ports
