#include "scheduling/application/create_series.hpp"

namespace pdr::scheduling {

CreateSeries::CreateSeries(ports::RecurrenceRepository& series,
                           const application::ports::IdGenerator& ids) noexcept
    : series_{series}, ids_{ids} {}

core::Result<RecurrenceSeries> CreateSeries::Execute(const Request& request) const {
    const auto rule = RecurrenceRule::Parse(request.rrule);
    if (!rule.HasValue()) {
        return rule.Failure();
    }

    const auto composed = RecurrenceSeries::Compose(ids_.Next<core::SeriesId>(),
                                                    request.tenant,
                                                    request.tutor,
                                                    {request.student},
                                                    rule.Value(),
                                                    request.starts_on,
                                                    request.at,
                                                    request.zone,
                                                    request.duration);
    if (!composed.HasValue()) {
        return composed.Failure();
    }

    const auto created = series_.Create(composed.Value());
    if (!created.HasValue()) {
        return created.Failure();
    }

    return composed.Value();
}

}  // namespace pdr::scheduling
