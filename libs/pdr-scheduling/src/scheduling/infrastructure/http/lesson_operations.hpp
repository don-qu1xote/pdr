#pragma once

#include <string_view>

#include <pdr/api/openapi.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/yaml_config/schema.hpp>

#include "application/ports/id_generator.hpp"
#include "events/bus.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/operation.hpp"
#include "scheduling/infrastructure/http/parts.hpp"

namespace pdr::scheduling::http {

/// ЗАНЯТИЯ ЗА ОТРЕЗОК. Один адрес на репетитора, ученика и опекуна.
class ListLessonsHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Nothing,
                                                     api::Lessons> {
public:
    explicit ListLessonsHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Nothing& body) const override;

    core::Result<api::Lessons> Run(const Call& call) const override;
};

/// ОДНО ЗАНЯТИЕ из расписания названного человека.
class GetLessonHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Nothing,
                                                     api::Lesson> {
public:
    explicit GetLessonHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Nothing& body) const override;

    core::Result<api::Lesson> Run(const Call& call) const override;
};

/// ЗАПИСАТЬ УЧЕНИКА НА ЗАНЯТИЕ.
class CreateLessonHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::NewLesson,
                                                     api::Lesson> {
public:
    explicit CreateLessonHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::NewLesson& body) const override;

    core::Result<api::Lesson> Run(const Call& call) const override;

    const application::ports::IdGenerator& ids_;
    events::Bus& bus_;
};

class ListLessonsOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-list-lessons";

    ListLessonsOperation(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    ListLessonsHandler handler_;
};

class GetLessonOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-get-lesson";

    GetLessonOperation(const userver::components::ComponentConfig& config,
                       const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    GetLessonHandler handler_;
};

class CreateLessonOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-create-lesson";

    CreateLessonOperation(const userver::components::ComponentConfig& config,
                          const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    CreateLessonHandler handler_;
};

}  // namespace pdr::scheduling::http
