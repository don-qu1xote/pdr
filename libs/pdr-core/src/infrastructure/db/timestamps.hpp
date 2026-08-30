#pragma once

#include <chrono>

#include <userver/storages/postgres/io/chrono.hpp>

#include "core/types/time.hpp"

namespace pdr::infrastructure::db {

/// `timestamptz` в записи драйвера. Момент домена и момент базы — разные типы,
/// и перевод между ними живёт в одном месте: четыре копии одного и того же
/// `duration_cast` рано или поздно разъезжаются на одной.
using Timestamptz = userver::storages::postgres::TimePointTz;

inline Timestamptz AsTimestamptz(core::Instant instant) {
    return Timestamptz{userver::storages::postgres::TimePoint{
        std::chrono::duration_cast<userver::storages::postgres::TimePoint::duration>(
            std::chrono::microseconds{instant.UnixMicros()})}};
}

inline core::Instant AsInstant(Timestamptz value) {
    return core::Instant::FromUnixMicros(std::chrono::duration_cast<std::chrono::microseconds>(
                                             value.GetUnderlying().time_since_epoch())
                                             .count());
}

}  // namespace pdr::infrastructure::db
