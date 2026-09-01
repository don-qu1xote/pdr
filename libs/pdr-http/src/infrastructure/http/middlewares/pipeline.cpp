#include "infrastructure/http/middlewares/pipeline.hpp"

#include <string>

namespace pdr::infrastructure::http {

Prepared& PreparedIn(userver::server::request::RequestContext& context) {
    auto* found = context.GetDataOptional<Prepared>(kPreparedName);
    if (found != nullptr) {
        return *found;
    }
    return context.EmplaceData<Prepared>(std::string{kPreparedName});
}

const Prepared& PreparedOf(userver::server::request::RequestContext& context) {
    return PreparedIn(context);
}

}  // namespace pdr::infrastructure::http
