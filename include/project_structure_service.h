#pragma once

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace qornix::project_structure {

inline std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

inline std::string readFileBinary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline bool hasPrefix(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

class ProjectStructureService {
public:
    ProjectStructureService(std::filesystem::path root, std::string rootLabel)
        : root_(std::move(root)), rootLabel_(std::move(rootLabel)) {}

    boost::json::object listDirectory(const std::string& requestedPath) const {
        const auto relative = normalizeRelativePath(requestedPath);
        const auto target = root_ / relative;

        boost::json::object payload;
        payload["rootLabel"] = rootLabel_;
        payload["path"] = relative;
        payload["breadcrumbs"] = breadcrumbsFor(relative);

        std::error_code ec;
        if (!std::filesystem::exists(root_, ec)) {
            payload["ok"] = false;
            payload["error"] = "project root was not found";
            payload["entries"] = boost::json::array{};
            return payload;
        }

        if (!std::filesystem::exists(target, ec) || !std::filesystem::is_directory(target, ec)) {
            payload["ok"] = false;
            payload["error"] = "requested project directory was not found";
            payload["entries"] = boost::json::array{};
            return payload;
        }

        std::vector<boost::json::object> entries;
        for (const auto& entry : std::filesystem::directory_iterator(target, ec)) {
            if (ec) break;
            const auto rel = std::filesystem::relative(entry.path(), root_, ec).generic_string();
            if (ec || shouldHidePath(rel)) {
                ec.clear();
                continue;
            }
            entries.emplace_back(makeEntry(entry));
        }

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            const std::string typeA(a.at("type").as_string().c_str());
            const std::string typeB(b.at("type").as_string().c_str());
            if (typeA != typeB) return typeA == "directory";
            const std::string nameA(a.at("name").as_string().c_str());
            const std::string nameB(b.at("name").as_string().c_str());
            return nameA < nameB;
        });

        boost::json::array jsonEntries;
        for (auto& entry : entries) jsonEntries.emplace_back(std::move(entry));

        payload["ok"] = true;
        payload["entries"] = std::move(jsonEntries);
        return payload;
    }

    std::filesystem::path createProjectArchive(std::string& error) const {
        std::error_code ec;
        if (!std::filesystem::exists(root_, ec)) {
            error = "project root was not found";
            return {};
        }

        const auto archivePath = std::filesystem::temp_directory_path(ec) / (rootLabel_ + "-project.tar.gz");
        if (ec) {
            error = "failed to resolve temporary directory";
            return {};
        }

        std::filesystem::remove(archivePath, ec);

        const std::string command =
            "tar -czf " + shellQuote(archivePath.string()) +
            " --exclude=./build" +
            " --exclude=./cmake-build-*" +
            " --exclude=./frontend/node_modules" +
            " --exclude=./frontend/dist" +
            " --exclude=./node_modules" +
            " --exclude=./.git" +
            " -C " + shellQuote(root_.string()) +
            " .";

        const int result = std::system(command.c_str());
        if (result != 0 || !std::filesystem::exists(archivePath, ec)) {
            error = "failed to create project archive; make sure tar is installed";
            return {};
        }
        return archivePath;
    }

private:
    std::filesystem::path root_;
    std::string rootLabel_;

    std::string normalizeRelativePath(std::string value) const {
        std::replace(value.begin(), value.end(), '\\', '/');
        while (!value.empty() && value.front() == '/') value.erase(value.begin());
        std::filesystem::path normalized;
        for (const auto& part : std::filesystem::path(value)) {
            const auto segment = part.string();
            if (segment.empty() || segment == "." || segment == "..") continue;
            normalized /= segment;
        }
        return normalized.generic_string();
    }

    bool shouldHidePath(const std::string& relativePath) const {
        return relativePath == ".git" || relativePath == "build" || hasPrefix(relativePath, "build/")
            || hasPrefix(relativePath, "cmake-build-") || relativePath == "node_modules"
            || hasPrefix(relativePath, "node_modules/") || relativePath == "frontend/node_modules"
            || hasPrefix(relativePath, "frontend/node_modules/");
    }

    std::string displayNameFor(const std::filesystem::path& path, bool directory) const {
        auto name = path.filename().string();
        if (name.empty()) name = rootLabel_;
        if (directory) name += "/";
        return name;
    }

    static std::string kindFor(const std::filesystem::path& path, bool directory) {
        if (directory) return "folder";
        const auto ext = path.extension().string();
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp") return "c++";
        if (ext == ".tsx" || ext == ".ts" || ext == ".jsx" || ext == ".js") return "typescript";
        if (ext == ".vue") return "vue";
        if (ext == ".json") return "json";
        if (ext == ".md") return "markdown";
        if (ext == ".xml" || ext == ".xsd") return "xml";
        if (ext == ".yaml" || ext == ".yml") return "yaml";
        if (ext == ".html") return "html";
        if (ext == ".css") return "css";
        if (ext == ".sh") return "script";
        return "file";
    }

    static std::string descriptionFor(const std::string& relativePath, bool directory) {
        if (relativePath.empty()) return "Generated application root.";
        if (relativePath == "backend") return "Qornix Web C++ backend, routes, handlers, configuration and backend assets.";
        if (relativePath == "handlers" || relativePath == "backend/handlers") return "Custom C++ handlers and business operations live here.";
        if (relativePath == "routes.h" || relativePath == "backend/routes.h") return "Central route registration for pages, APIs and custom endpoints.";
        if (relativePath == "backend/services") return "Template-local backend services used by route handlers.";
        if (relativePath == "backend/schema" || relativePath == "schema") return "Dynamic API XML schema and schema history files.";
        if (relativePath == "backend/templates" || relativePath == "templates") return "HTML templates served by the generated backend.";
        if (relativePath == "backend/static" || relativePath == "static") return "Static assets used by generated pages.";
        if (relativePath == "frontend") return "Frontend workspace for React or Vue templates.";
        if (relativePath == "frontend/src") return "Frontend application source code.";
        if (relativePath == "CMakeLists.txt") return "Build configuration for the generated application.";
        if (relativePath == "README.md") return "Generated project guide for backend and frontend developers.";
        if (relativePath == "runtime-Dockerfile") return "Runtime image definition for the portable deploy bundle.";
        if (relativePath == "package.json") return "Optional helper scripts or frontend package definition.";
        if (directory) return "Project directory.";
        return "Project file.";
    }

    boost::json::object makeBreadcrumb(const std::string& label, const std::string& path) const {
        boost::json::object item;
        item["label"] = label;
        item["path"] = path;
        return item;
    }

    boost::json::array breadcrumbsFor(const std::string& relativePath) const {
        boost::json::array crumbs;
        crumbs.emplace_back(makeBreadcrumb(rootLabel_, ""));
        std::filesystem::path current;
        for (const auto& part : std::filesystem::path(relativePath)) {
            const auto label = part.string();
            if (label.empty() || label == ".") continue;
            current /= label;
            crumbs.emplace_back(makeBreadcrumb(label, current.generic_string()));
        }
        return crumbs;
    }

    boost::json::object makeEntry(const std::filesystem::directory_entry& entry) const {
        std::error_code ec;
        const auto absolute = entry.path();
        const auto relative = std::filesystem::relative(absolute, root_, ec).generic_string();
        const bool directory = entry.is_directory(ec);
        const bool regularFile = entry.is_regular_file(ec);

        boost::json::object item;
        item["name"] = displayNameFor(absolute, directory);
        item["path"] = relative;
        item["kind"] = kindFor(absolute, directory);
        item["type"] = directory ? "directory" : "file";
        item["description"] = descriptionFor(relative, directory);
        item["size"] = regularFile ? static_cast<std::uint64_t>(entry.file_size(ec)) : 0;
        item["downloadable"] = regularFile;
        item["navigable"] = directory;
        return item;
    }
};

} // namespace qornix::project_structure
