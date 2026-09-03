#pragma once

#include <algorithm>

#include <boost/uuid/uuid.hpp>

#include <userver/storages/postgres/io/transform_io.hpp>
#include <userver/storages/postgres/io/type_mapping.hpp>
#include <userver/storages/postgres/io/uuid.hpp>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::infrastructure::db {

/// Идентификатор домена — шестнадцать байт, и `uuid` базы — те же шестнадцать.
/// Перекладывание, а не разбор: ошибиться тут нечем и падать негде.
struct IdAsUuid final {
    template<class Tag>
    boost::uuids::uuid operator()(const core::StrongId<Tag>& id) const noexcept {
        boost::uuids::uuid raw{};
        const auto& bytes = id.AsBytes();
        std::copy(bytes.begin(), bytes.end(), raw.begin());
        return raw;
    }
};

struct InstantAsTimestamptz final {
    Timestamptz operator()(core::Instant instant) const {
        return AsTimestamptz(instant);
    }
};

}  // namespace pdr::infrastructure::db

USERVER_NAMESPACE_BEGIN

namespace storages::postgres::io {

template<class Tag>
struct BufferFormatter<pdr::core::StrongId<Tag>>
    : TransformFormatter<pdr::core::StrongId<Tag>,
                         boost::uuids::uuid,
                         pdr::infrastructure::db::IdAsUuid> {
    using BaseType = TransformFormatter<pdr::core::StrongId<Tag>,
                                        boost::uuids::uuid,
                                        pdr::infrastructure::db::IdAsUuid>;
    using BaseType::BaseType;
};

template<class Tag>
struct CppToSystemPg<pdr::core::StrongId<Tag>> : PredefinedOid<PredefinedOids::kUuid> {};

template<>
struct BufferFormatter<pdr::core::Instant>
    : TransformFormatter<pdr::core::Instant,
                         pdr::infrastructure::db::Timestamptz,
                         pdr::infrastructure::db::InstantAsTimestamptz> {
    using BaseType = TransformFormatter<pdr::core::Instant,
                                        pdr::infrastructure::db::Timestamptz,
                                        pdr::infrastructure::db::InstantAsTimestamptz>;
    using BaseType::BaseType;
};

template<>
struct CppToSystemPg<pdr::core::Instant> : PredefinedOid<PredefinedOids::kTimestamptz> {};

}  // namespace storages::postgres::io

USERVER_NAMESPACE_END
