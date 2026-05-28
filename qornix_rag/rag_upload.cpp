/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_upload.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace qornix::rag {
namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        std::string out;
        out.reserve(value.size() - 2);
        bool escape = false;
        for (size_t i = 1; i + 1 < value.size(); ++i) {
            const char ch = value[i];
            if (escape) {
                out.push_back(ch);
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else {
                out.push_back(ch);
            }
        }
        return out;
    }
    return value;
}

std::map<std::string, std::string> parseHeaderParams(const std::string& header_value) {
    std::map<std::string, std::string> params;
    std::string token;
    bool in_quote = false;
    bool escape = false;
    std::vector<std::string> segments;
    for (char ch : header_value) {
        if (escape) {
            token.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\' && in_quote) {
            token.push_back(ch);
            escape = true;
            continue;
        }
        if (ch == '"') {
            in_quote = !in_quote;
            token.push_back(ch);
            continue;
        }
        if (ch == ';' && !in_quote) {
            segments.push_back(token);
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    segments.push_back(token);

    for (size_t i = 0; i < segments.size(); ++i) {
        auto segment = trim(segments[i]);
        if (segment.empty()) continue;
        auto eq = segment.find('=');
        if (eq == std::string::npos) {
            if (i == 0) {
                params["_type"] = lower(segment);
            }
            continue;
        }
        params[lower(trim(segment.substr(0, eq)))] = unquote(segment.substr(eq + 1));
    }
    return params;
}

std::string extractBoundary(const std::string& content_type_header) {
    const auto params = parseHeaderParams(content_type_header);
    auto type_it = params.find("_type");
    if (type_it == params.end() || type_it->second.find("multipart/form-data") == std::string::npos) {
        return {};
    }
    auto boundary_it = params.find("boundary");
    if (boundary_it == params.end()) {
        return {};
    }
    return boundary_it->second;
}

std::map<std::string, std::string> parsePartHeaders(const std::string& raw_headers) {
    std::map<std::string, std::string> headers;
    std::istringstream in(raw_headers);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return headers;
}

std::string makeUploadBatchId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::ostringstream out;
    out << "upload_" << millis;
    return out.str();
}

std::string cheapContentHash(const std::string& content) {
    // This is intentionally a compact non-cryptographic digest for response/debug
    // metadata only. It is not used as a trust/security primitive.
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

bool mimeAllowed(const std::vector<std::string>& allowed, const std::string& normalized_mime) {
    if (normalized_mime.empty()) {
        return true;
    }
    for (const auto& raw : allowed) {
        const auto pattern = lower(trim(raw));
        if (pattern.empty()) continue;
        if (pattern == normalized_mime) return true;
        if (pattern.size() > 2 && pattern.substr(pattern.size() - 2) == "/*") {
            const auto prefix = pattern.substr(0, pattern.size() - 1);
            if (startsWith(normalized_mime, prefix)) return true;
        }
    }
    return false;
}

fs::path canonicalBase(const std::string& uploads_dir) {
    std::error_code ec;
    fs::create_directories(uploads_dir, ec);
    return fs::weakly_canonical(fs::absolute(uploads_dir), ec);
}

bool isPathInside(const fs::path& child, const fs::path& base) {
    auto child_abs = fs::weakly_canonical(fs::absolute(child));
    auto base_abs = fs::weakly_canonical(fs::absolute(base));
    auto child_it = child_abs.begin();
    auto base_it = base_abs.begin();
    for (; base_it != base_abs.end(); ++base_it, ++child_it) {
        if (child_it == child_abs.end() || *child_it != *base_it) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::string> splitUploadCsvList(const std::string& csv) {
    std::vector<std::string> values;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

std::string normalizeUploadExtension(const std::string& filename) {
    return lower(fs::path(filename).extension().string());
}

std::string sanitizeUploadFilename(const std::string& filename) {
    std::string base = fs::path(filename).filename().string();
    if (base.empty() || base == "." || base == "..") {
        base = "upload.bin";
    }
    std::string out;
    out.reserve(base.size());
    for (unsigned char ch : base) {
        if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    while (out.find("..") != std::string::npos) {
        out.replace(out.find(".."), 2, ".");
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '_' || out.front() == '-')) {
        out.erase(out.begin());
    }
    if (out.empty()) {
        out = "upload.bin";
    }
    return out;
}

std::string normalizeUploadMime(const std::string& content_type) {
    auto value = lower(trim(content_type));
    const auto semi = value.find(';');
    if (semi != std::string::npos) {
        value = trim(value.substr(0, semi));
    }
    return value;
}

RagUploadService::RagUploadService(RagUploadConfig config)
    : config_(std::move(config)) {
    for (auto& ext : config_.allowed_extensions) {
        ext = lower(trim(ext));
        if (!ext.empty() && ext.front() != '.') {
            ext.insert(ext.begin(), '.');
        }
    }
    for (auto& mime : config_.allowed_mime_types) {
        mime = lower(trim(mime));
    }
}

RagUploadValidationResult RagUploadService::validateFile(const std::string& filename,
                                                         const std::string& content_type,
                                                         size_t size_bytes) const {
    RagUploadValidationResult result;
    result.sanitized_filename = sanitizeUploadFilename(filename);
    result.extension = normalizeUploadExtension(result.sanitized_filename);
    result.normalized_mime = normalizeUploadMime(content_type);

    if (!config_.enabled) {
        result.code = "upload_disabled";
        result.message = "Document uploads are disabled by configuration";
        return result;
    }
    if (filename.empty()) {
        result.code = "missing_filename";
        result.message = "Uploaded part has no filename";
        return result;
    }
    if (result.extension.empty()) {
        result.code = "missing_extension";
        result.message = "Uploaded file has no extension";
        return result;
    }
    const auto ext_it = std::find(config_.allowed_extensions.begin(), config_.allowed_extensions.end(), result.extension);
    if (ext_it == config_.allowed_extensions.end()) {
        result.code = "extension_not_allowed";
        result.message = "File extension is not allowed: " + result.extension;
        return result;
    }
    const size_t max_bytes = config_.max_file_size_kb * 1024;
    if (max_bytes > 0 && size_bytes > max_bytes) {
        result.code = "file_too_large";
        result.message = "File exceeds upload size limit";
        return result;
    }
    if (!mimeAllowed(config_.allowed_mime_types, result.normalized_mime)) {
        result.code = "mime_not_allowed";
        result.message = "Content-Type is not allowed: " + result.normalized_mime;
        return result;
    }

    result.ok = true;
    result.code = "ok";
    result.message = "File accepted";
    return result;
}

RagMultipartParseResult RagUploadService::parseMultipart(const std::string& content_type_header,
                                                         const std::string& body) const {
    RagMultipartParseResult result;
    const std::string boundary = extractBoundary(content_type_header);
    if (boundary.empty()) {
        result.error = "Content-Type must be multipart/form-data with a boundary";
        return result;
    }

    const std::string delimiter = "--" + boundary;
    size_t pos = body.find(delimiter);
    if (pos == std::string::npos) {
        result.error = "Multipart boundary was not found in request body";
        return result;
    }
    pos += delimiter.size();

    while (pos < body.size()) {
        if (body.compare(pos, 2, "--") == 0) {
            result.ok = true;
            return result;
        }
        if (body.compare(pos, 2, "\r\n") == 0) {
            pos += 2;
        } else if (body.compare(pos, 1, "\n") == 0) {
            pos += 1;
        }

        const size_t next = body.find(delimiter, pos);
        if (next == std::string::npos) {
            break;
        }
        std::string part = body.substr(pos, next - pos);
        if (part.size() >= 2 && part.compare(part.size() - 2, 2, "\r\n") == 0) {
            part.resize(part.size() - 2);
        } else if (!part.empty() && part.back() == '\n') {
            part.pop_back();
        }

        size_t header_end = part.find("\r\n\r\n");
        size_t separator_len = 4;
        if (header_end == std::string::npos) {
            header_end = part.find("\n\n");
            separator_len = 2;
        }
        if (header_end != std::string::npos) {
            auto headers = parsePartHeaders(part.substr(0, header_end));
            auto disp_it = headers.find("content-disposition");
            if (disp_it != headers.end()) {
                const auto disp = parseHeaderParams(disp_it->second);
                const auto name_it = disp.find("name");
                const auto filename_it = disp.find("filename");
                std::string content = part.substr(header_end + separator_len);
                if (filename_it != disp.end() && !filename_it->second.empty()) {
                    RagUploadPart file;
                    file.field_name = name_it == disp.end() ? "files" : name_it->second;
                    file.filename = filename_it->second;
                    auto ct_it = headers.find("content-type");
                    file.content_type = ct_it == headers.end() ? std::string{} : ct_it->second;
                    file.content = std::move(content);
                    result.files.push_back(std::move(file));
                } else if (name_it != disp.end()) {
                    result.fields[name_it->second] = std::move(content);
                }
            }
        }

        pos = next + delimiter.size();
    }

    result.ok = true;
    return result;
}

std::vector<RagStoredUpload> RagUploadService::storeFiles(const std::vector<RagUploadPart>& parts,
                                                          std::string* batch_id) const {
    if (parts.empty()) {
        throw std::runtime_error("No upload files provided");
    }
    if (config_.max_files_per_request > 0 && parts.size() > config_.max_files_per_request) {
        throw std::runtime_error("Too many files in upload request");
    }

    const std::string batch = makeUploadBatchId();
    if (batch_id) {
        *batch_id = batch;
    }

    const fs::path base = canonicalBase(config_.uploads_dir);
    const fs::path batch_dir = base / batch;
    fs::create_directories(batch_dir);

    std::vector<RagStoredUpload> stored;
    stored.reserve(parts.size());
    for (const auto& part : parts) {
        auto validation = validateFile(part.filename, part.content_type, part.content.size());
        if (!validation.ok) {
            throw std::runtime_error(validation.message);
        }

        fs::path target = batch_dir / validation.sanitized_filename;
        if (!config_.overwrite_existing) {
            const fs::path stem = target.stem();
            const fs::path ext = target.extension();
            size_t counter = 2;
            while (fs::exists(target)) {
                target = batch_dir / (stem.string() + "-" + std::to_string(counter++) + ext.string());
            }
        }
        if (!isPathInside(target, base)) {
            throw std::runtime_error("Upload target path escaped uploads_dir");
        }

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Failed to open upload target for writing: " + target.string());
        }
        out.write(part.content.data(), static_cast<std::streamsize>(part.content.size()));
        out.close();

        RagStoredUpload item;
        item.original_filename = part.filename;
        item.stored_filename = target.filename().string();
        item.path = target.string();
        std::error_code ec;
        item.relative_path = fs::relative(target, fs::current_path(), ec).string();
        if (ec || item.relative_path.empty()) {
            item.relative_path = target.string();
        }
        item.content_type = normalizeUploadMime(part.content_type);
        item.size_bytes = part.content.size();
        item.content_hash = cheapContentHash(part.content);
        stored.push_back(std::move(item));
    }
    return stored;
}

bool RagUploadService::removeStoredFile(const std::string& relative_or_absolute_path,
                                        std::string* removed_absolute_path,
                                        std::string* removed_relative_path) const {
    if (relative_or_absolute_path.empty()) {
        return false;
    }
    const fs::path base = canonicalBase(config_.uploads_dir);
    fs::path target(relative_or_absolute_path);
    if (target.is_relative()) {
        target = fs::current_path() / target;
    }
    std::error_code ec;
    target = fs::weakly_canonical(target, ec);
    if (ec || !isPathInside(target, base) || !fs::is_regular_file(target, ec)) {
        return false;
    }
    if (!fs::remove(target, ec) || ec) {
        return false;
    }
    if (removed_absolute_path) {
        *removed_absolute_path = target.string();
    }
    if (removed_relative_path) {
        std::error_code rel_ec;
        *removed_relative_path = fs::relative(target, fs::current_path(), rel_ec).string();
        if (rel_ec || removed_relative_path->empty()) {
            *removed_relative_path = target.string();
        }
    }
    return true;
}

} // namespace qornix::rag
