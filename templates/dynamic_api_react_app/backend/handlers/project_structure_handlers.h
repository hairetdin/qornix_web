#pragma once

#include "handler_base.h"
#include "app_paths.h"
#include "project_structure_service.h"

#include <boost/json/serialize.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace dynamic_api_react_project_handlers {

inline std::string queryParam(const urls::url_view& url, const std::string& key) {
    for (const auto& param : url.params()) {
        if (std::string(param.key) == key) {
            return std::string(param.value);
        }
    }
    return "";
}

} // namespace dynamic_api_react_project_handlers

class ProjectStructureListHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view& url,
        const std::map<std::string, std::string>&
    ) override {
        const auto path = dynamic_api_react_project_handlers::queryParam(url, "path");
        qornix::project_structure::ProjectStructureService service(dynamic_api_app_paths::sourceDir(), "@PROJECT_NAME@");
        auto payload = service.listDirectory(path);
        buildJsonResponse(res, http::status::ok, boost::json::serialize(payload));
    }
};

class ProjectArchiveHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        std::string error;
        qornix::project_structure::ProjectStructureService service(dynamic_api_app_paths::sourceDir(), "@PROJECT_NAME@");
        const auto archivePath = service.createProjectArchive(error);
        if (archivePath.empty()) {
            boost::json::object payload;
            payload["error"] = error.empty() ? "failed to create project archive" : error;
            res = make_json_response(http::status::internal_server_error, req.version(), boost::json::serialize(payload));
            return;
        }

        auto body = qornix::project_structure::readFileBinary(archivePath);
        std::error_code ec;
        std::filesystem::remove(archivePath, ec);

        res = http::response<http::string_body>{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/gzip");
        res.set(http::field::content_disposition, "attachment; filename=\"@PROJECT_NAME@-project.tar.gz\"");
        res.body() = std::move(body);
        res.prepare_payload();
    }
};

inline std::shared_ptr<ProjectStructureListHandler> makeProjectStructureListHandler() {
    return std::make_shared<ProjectStructureListHandler>();
}

inline std::shared_ptr<ProjectArchiveHandler> makeProjectArchiveHandler() {
    return std::make_shared<ProjectArchiveHandler>();
}
