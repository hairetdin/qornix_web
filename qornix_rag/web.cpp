/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "web.h"
#include "memory_source.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <map>
#include <string_view>
#include <limits>
#include <set>

namespace http = boost::beast::http;
namespace urls = boost::urls;

using qornix::rag::QASource;
using qornix::rag::MemorySource;
using qornix::rag::RagStoredUpload;
using qornix::rag::RagUploadService;
// DataSource and DataSourceType are in global namespace (defined in core.h)

namespace {

std::string normalizeRoutePrefix(std::string prefix, const std::string& fallback) {
    if (prefix.empty()) {
        prefix = fallback;
    }
    if (prefix.front() != '/') {
        prefix.insert(prefix.begin(), '/');
    }
    while (prefix.size() > 1 && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix;
}

std::string joinRoute(const std::string& prefix, const std::string& suffix) {
    if (prefix == "/") {
        return suffix.empty() ? "/" : suffix;
    }
    if (suffix.empty() || suffix == "/") {
        return prefix;
    }
    if (suffix.front() == '/') {
        return prefix + suffix;
    }
    return prefix + "/" + suffix;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool apiPathMatches(const std::string& path, const std::string& legacy_path) {
    if (path == legacy_path) {
        return true;
    }
    constexpr const char* legacy_prefix = "/api/";
    if (legacy_path.rfind(legacy_prefix, 0) != 0 || path.rfind(legacy_prefix, 0) != 0) {
        return false;
    }
    const std::string suffix = "/" + legacy_path.substr(std::char_traits<char>::length(legacy_prefix));
    return endsWith(path, suffix);
}

std::string headerValue(const http::request<http::string_body>& req, const std::string& name) {
    auto it = req.find(name);
    if (it == req.end()) {
        return "";
    }
    return std::string(it->value().data(), it->value().size());
}

std::string bearerToken(const std::string& authorization) {
    constexpr const char* prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0) {
        return "";
    }
    return authorization.substr(std::char_traits<char>::length(prefix));
}

std::string configuredAdminToken(const RagRouteAuthOptions& options) {
    if (!options.admin_token.empty()) {
        return options.admin_token;
    }
    if (!options.admin_token_env.empty()) {
        if (const char* value = std::getenv(options.admin_token_env.c_str())) {
            return value;
        }
    }
    return "";
}

bool isAdminRoute(const std::string& path) {
    return apiPathMatches(path, "/api/admin/diagnostics") ||
           apiPathMatches(path, "/api/metrics") ||
           apiPathMatches(path, "/api/ingest/jobs") ||
           path.find("/ingest/") != std::string::npos;
}

bool isWriteRoute(const std::string& path, const std::string& method) {
    if (method == "GET" || method == "HEAD" || method == "OPTIONS") {
        return false;
    }
    if ((method == "PUT" || method == "DELETE") && path.find("/qa/") != std::string::npos) {
        return true;
    }
    return apiPathMatches(path, "/api/index") ||
           apiPathMatches(path, "/api/ingest") ||
           apiPathMatches(path, "/api/documents/upload") ||
           apiPathMatches(path, "/api/uploads/delete") ||
           apiPathMatches(path, "/api/embedding/switch") ||
           apiPathMatches(path, "/api/documents/delete") ||
           apiPathMatches(path, "/api/feedback") ||
           apiPathMatches(path, "/api/sources/add") ||
           apiPathMatches(path, "/api/sources/remove") ||
           apiPathMatches(path, "/api/qa") ||
           apiPathMatches(path, "/api/qa/add") ||
           apiPathMatches(path, "/api/qa/duplicate-check") ||
           apiPathMatches(path, "/api/qa/update") ||
           apiPathMatches(path, "/api/qa/delete") ||
           apiPathMatches(path, "/api/qa/import") ||
           apiPathMatches(path, "/api/qa/export");
}

std::string makeQaPairId(const std::string& source_id) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return source_id + "_qa_" + std::to_string(millis);
}

std::string qaContextPath(const QASource::QAPair& pair) {
    return "qa://" + (pair.category.empty() ? std::string("general") : pair.category) + "/" + pair.id;
}

std::string qaContextSnippet(const QASource::QAPair& pair) {
    return "Q: " + pair.question + "\nA: " + pair.answer;
}

boost::json::array stringArrayJson(const std::vector<std::string>& values) {
    boost::json::array array;
    for (const auto& value : values) {
        if (!value.empty()) {
            array.emplace_back(value);
        }
    }
    return array;
}

std::vector<std::string> jsonStringArray(const boost::json::object& obj, const char* key) {
    std::vector<std::string> values;
    if (!obj.contains(key) || !obj.at(key).is_array()) {
        return values;
    }
    for (const auto& value : obj.at(key).as_array()) {
        if (value.is_string()) {
            values.emplace_back(value.as_string().c_str());
        }
    }
    return values;
}

std::map<std::string, std::string> jsonStringMap(const boost::json::object& obj, const char* key) {
    std::map<std::string, std::string> values;
    if (!obj.contains(key) || !obj.at(key).is_object()) {
        return values;
    }
    for (const auto& entry : obj.at(key).as_object()) {
        if (entry.value().is_string()) {
            values[std::string(entry.key())] = entry.value().as_string().c_str();
        }
    }
    return values;
}

std::string jsonScalarToString(const boost::json::value& value) {
    if (value.is_string()) {
        return value.as_string().c_str();
    }
    if (value.is_int64()) {
        return std::to_string(value.as_int64());
    }
    if (value.is_uint64()) {
        return std::to_string(value.as_uint64());
    }
    if (value.is_double()) {
        std::ostringstream out;
        out << value.as_double();
        return out.str();
    }
    if (value.is_bool()) {
        return value.as_bool() ? "true" : "false";
    }
    return {};
}

boost::json::object stringMapJson(const std::map<std::string, std::string>& values) {
    boost::json::object object;
    for (const auto& [key, value] : values) {
        if (!key.empty()) {
            object[key] = value;
        }
    }
    return object;
}


boost::json::array retrievalQueriesJson(const std::vector<RagServiceRetrievalQuery>& queries) {
    boost::json::array array;
    for (const auto& query : queries) {
        boost::json::object object;
        object["query"] = query.query;
        object["origin"] = query.origin;
        object["result_count"] = static_cast<std::int64_t>(query.result_count);
        array.emplace_back(std::move(object));
    }
    return array;
}

boost::json::array groundingClaimsJson(const std::vector<RagServiceGroundingClaim>& claims) {
    boost::json::array array;
    for (const auto& claim : claims) {
        boost::json::object object;
        object["claim"] = claim.claim;
        object["status"] = claim.status;
        object["support_score"] = claim.support_score;
        object["citations"] = stringArrayJson(claim.citations);
        array.emplace_back(std::move(object));
    }
    return array;
}

boost::json::object feedbackJson(const AnalyticsService::FeedbackEntry& feedback) {
    boost::json::object object;
    object["id"] = feedback.id;
    object["request_id"] = feedback.request_id;
    object["query"] = feedback.query;
    object["question"] = feedback.question;
    object["rating"] = feedback.rating;
    object["category"] = feedback.category;
    object["comment"] = feedback.comment;
    object["citations"] = stringArrayJson(feedback.citations);
    object["client_ip"] = feedback.client_ip;
    return object;
}

boost::json::array metadataFiltersJson(const std::vector<MetadataFilter>& filters) {
    boost::json::array array;
    for (const auto& filter : filters) {
        boost::json::object object;
        object["key"] = filter.key;
        object["op"] = filter.op;
        object["value"] = filter.value;
        array.emplace_back(object);
    }
    return array;
}

void appendFilterFromObject(std::vector<MetadataFilter>& filters,
                            const boost::json::object& obj,
                            const std::string& default_op = "equals") {
    std::string key;
    std::string value;
    std::string op = default_op;
    if (obj.contains("key") && obj.at("key").is_string()) {
        key = obj.at("key").as_string().c_str();
    }
    if (obj.contains("value")) {
        value = jsonScalarToString(obj.at("value"));
    }
    if (obj.contains("op") && obj.at("op").is_string()) {
        op = obj.at("op").as_string().c_str();
    }
    if (!key.empty()) {
        filters.emplace_back(key, value, op);
    }
}

std::vector<MetadataFilter> parseMetadataFilters(const boost::json::object& obj) {
    std::vector<MetadataFilter> filters;
    auto parseKeyValueObject = [&](const boost::json::object& kv, const std::string& op) {
        for (const auto& entry : kv) {
            const std::string key(entry.key());
            const std::string value = jsonScalarToString(entry.value());
            if (!key.empty()) {
                filters.emplace_back(key, value, op);
            }
        }
    };

    auto parseFiltersValue = [&](const boost::json::value& value) {
        if (value.is_array()) {
            for (const auto& item : value.as_array()) {
                if (item.is_object()) {
                    appendFilterFromObject(filters, item.as_object());
                }
            }
            return;
        }
        if (!value.is_object()) {
            return;
        }
        const auto& fobj = value.as_object();
        for (const auto& entry : fobj) {
            const std::string key(entry.key());
            if (key == "metadata" && entry.value().is_object()) {
                parseKeyValueObject(entry.value().as_object(), "equals");
            } else if (key == "metadata_contains" && entry.value().is_object()) {
                parseKeyValueObject(entry.value().as_object(), "contains");
            } else if (key == "metadata_gte" && entry.value().is_object()) {
                parseKeyValueObject(entry.value().as_object(), "gte");
            } else if (key == "metadata_lte" && entry.value().is_object()) {
                parseKeyValueObject(entry.value().as_object(), "lte");
            } else if (entry.value().is_object()) {
                appendFilterFromObject(filters, entry.value().as_object());
            } else {
                filters.emplace_back(key, jsonScalarToString(entry.value()), "equals");
            }
        }
    };

    if (obj.contains("filters")) {
        parseFiltersValue(obj.at("filters"));
    }
    if (obj.contains("metadata_filters")) {
        parseFiltersValue(obj.at("metadata_filters"));
    }
    return filters;
}

std::string htmlEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char ch : input) {
        switch (ch) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output.push_back(ch); break;
        }
    }
    return output;
}

bool isSafeMarkdownUrl(const std::string& url) {
    const auto colon = url.find(':');
    if (colon == std::string::npos) {
        return !url.empty() && url.rfind("//", 0) != 0;
    }
    std::string scheme = url.substr(0, colon);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return scheme == "http" || scheme == "https" || scheme == "mailto";
}

std::string renderMarkdownInlineSanitized(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '[') {
            const size_t close_label = text.find(']', i + 1);
            if (close_label != std::string::npos && close_label + 1 < text.size() && text[close_label + 1] == '(') {
                const size_t close_url = text.find(')', close_label + 2);
                if (close_url != std::string::npos) {
                    const std::string label = text.substr(i + 1, close_label - i - 1);
                    const std::string url = text.substr(close_label + 2, close_url - close_label - 2);
                    if (isSafeMarkdownUrl(url)) {
                        output += "<a href=\"" + htmlEscape(url) + "\" rel=\"noopener noreferrer\" target=\"_blank\">";
                        output += htmlEscape(label);
                        output += "</a>";
                        i = close_url + 1;
                        continue;
                    }
                }
            }
        }
        if (text.compare(i, 2, "**") == 0) {
            const size_t close = text.find("**", i + 2);
            if (close != std::string::npos) {
                output += "<strong>" + htmlEscape(text.substr(i + 2, close - i - 2)) + "</strong>";
                i = close + 2;
                continue;
            }
        }
        if (text[i] == '`') {
            const size_t close = text.find('`', i + 1);
            if (close != std::string::npos) {
                output += "<code>" + htmlEscape(text.substr(i + 1, close - i - 1)) + "</code>";
                i = close + 1;
                continue;
            }
        }
        output += htmlEscape(std::string(1, text[i]));
        ++i;
    }
    return output;
}

std::string renderMarkdownLite(const std::string& markdown) {
    std::ostringstream html;
    std::istringstream in(markdown);
    std::string line;
    bool in_list = false;
    bool in_code = false;
    while (std::getline(in, line)) {
        if (line.rfind("```", 0) == 0) {
            if (in_list) { html << "</ul>"; in_list = false; }
            if (!in_code) {
                html << "<pre><code>";
                in_code = true;
            } else {
                html << "</code></pre>";
                in_code = false;
            }
            continue;
        }
        if (in_code) {
            html << htmlEscape(line) << "\n";
            continue;
        }
        if (line.empty()) {
            if (in_list) {
                html << "</ul>";
                in_list = false;
            }
            continue;
        }
        if (line.rfind("# ", 0) == 0) {
            if (in_list) { html << "</ul>"; in_list = false; }
            html << "<h1>" << renderMarkdownInlineSanitized(line.substr(2)) << "</h1>";
        } else if (line.rfind("## ", 0) == 0) {
            if (in_list) { html << "</ul>"; in_list = false; }
            html << "<h2>" << renderMarkdownInlineSanitized(line.substr(3)) << "</h2>";
        } else if (line.rfind("- ", 0) == 0) {
            if (!in_list) {
                html << "<ul>";
                in_list = true;
            }
            html << "<li>" << renderMarkdownInlineSanitized(line.substr(2)) << "</li>";
        } else {
            if (in_list) {
                html << "</ul>";
                in_list = false;
            }
            html << "<p>" << renderMarkdownInlineSanitized(line) << "</p>";
        }
    }
    if (in_code) {
        html << "</code></pre>";
    }
    if (in_list) {
        html << "</ul>";
    }
    return html.str();
}

boost::json::object qaContextObject(const QASource::QAPair& pair, double score) {
    boost::json::object obj;
    obj["path"] = qaContextPath(pair);
    obj["score"] = score;
    obj["snippet"] = qaContextSnippet(pair);
    obj["source_type"] = "qa";
    obj["category"] = pair.category;
    obj["pair_id"] = pair.id;
    obj["tags"] = stringArrayJson(pair.tags);
    auto attribution = pair.metadata.find("source");
    obj["attribution"] = attribution == pair.metadata.end() ? "QA Knowledge Base" : attribution->second;
    return obj;
}

boost::json::object qaSearchResultObject(const QASource::QAPair& pair, double score) {
    boost::json::object obj;
    obj["path"] = qaContextPath(pair);
    obj["type"] = "QA";
    obj["language"] = "knowledge_base";
    obj["score"] = score;
    obj["vector_score"] = 0.0;
    obj["text_score"] = score;
    obj["fused_score"] = score;
    obj["snippet"] = qaContextSnippet(pair);
    obj["lines"] = static_cast<std::int64_t>(2);
    obj["size"] = static_cast<std::int64_t>(pair.question.size() + pair.answer.size());
    obj["source_type"] = "qa";
    obj["category"] = pair.category;
    obj["pair_id"] = pair.id;
    obj["tags"] = stringArrayJson(pair.tags);
    auto attribution = pair.metadata.find("source");
    obj["attribution"] = attribution == pair.metadata.end() ? "QA Knowledge Base" : attribution->second;
    return obj;
}

boost::json::object ingestionJobObject(const RagServiceIngestionJob& job) {
    boost::json::object obj;
    obj["id"] = job.id;
    obj["source_id"] = job.source_id;
    obj["root_path"] = job.root_path;
    obj["status"] = job.status;
    obj["files_seen"] = static_cast<std::int64_t>(job.files_seen);
    obj["documents_imported"] = static_cast<std::int64_t>(job.documents_imported);
    obj["duplicates_found"] = static_cast<std::int64_t>(job.duplicates_found);
    obj["skipped"] = static_cast<std::int64_t>(job.skipped);
    obj["errors"] = static_cast<std::int64_t>(job.errors);
    obj["progress_percent"] = static_cast<std::int64_t>(job.progress_percent);
    obj["background"] = job.background;
    obj["error_message"] = job.error_message;
    obj["started_at"] = job.started_at;
    obj["finished_at"] = job.finished_at;
    return obj;
}

boost::json::object storedUploadObject(const RagStoredUpload& upload) {
    boost::json::object obj;
    obj["original_filename"] = upload.original_filename;
    obj["stored_filename"] = upload.stored_filename;
    obj["path"] = upload.path;
    obj["relative_path"] = upload.relative_path;
    obj["content_type"] = upload.content_type;
    obj["size_bytes"] = static_cast<std::int64_t>(upload.size_bytes);
    obj["content_hash"] = upload.content_hash;
    return obj;
}

bool uploadStringBool(const std::string& value, bool fallback) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") return true;
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") return false;
    return fallback;
}

boost::json::object embeddingModelObject(const RagServiceEmbeddingModelItem& model) {
    boost::json::object obj;
    obj["id"] = model.id;
    obj["backend"] = model.backend;
    obj["name"] = model.name;
    obj["version"] = model.version;
    obj["model_path"] = model.model_path;
    obj["tokenizer_path"] = model.tokenizer_path;
    obj["tokenizer_type"] = model.tokenizer_type;
    obj["pooling"] = model.pooling;
    obj["dimension"] = static_cast<std::int64_t>(model.dimension);
    obj["max_seq_len"] = static_cast<std::int64_t>(model.max_seq_len);
    obj["tokenizer_vocab_size"] = static_cast<std::int64_t>(model.tokenizer_vocab_size);
    obj["effective_chunk_token_limit"] = static_cast<std::int64_t>(model.effective_chunk_token_limit);
    obj["active"] = model.active;
    obj["ready"] = model.ready;
    obj["discovered"] = model.discovered;
    obj["files_present"] = model.files_present;
    obj["persistent_cache_enabled"] = model.persistent_cache_enabled;
    obj["status"] = model.status;
    obj["tokenizer_status"] = model.tokenizer_status;
    obj["model_status"] = model.model_status;
    obj["model_signature"] = model.model_signature;
    obj["source"] = model.source;
    return obj;
}

boost::json::object qaPairObject(const QASource::QAPair& pair) {
    boost::json::object pair_obj;
    pair_obj["id"] = pair.id;
    pair_obj["question"] = pair.question;
    pair_obj["answer"] = pair.answer;
    pair_obj["category"] = pair.category.empty() ? "general" : pair.category;

    pair_obj["answer_html"] = renderMarkdownLite(pair.answer);
    pair_obj["answer_html_sanitized"] = true;
    pair_obj["markdown_renderer"] = "qornix_markdown_safe_v1";
    pair_obj["aliases"] = stringArrayJson(pair.aliases);
    pair_obj["tags"] = stringArrayJson(pair.tags);
    boost::json::object metadata_obj;
    for (const auto& [key, value] : pair.metadata) {
        metadata_obj[key] = value;
    }
    pair_obj["metadata"] = metadata_obj;
    return pair_obj;
}


std::string normalizeQaDuplicateText(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool last_space = true;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c >= 128) {
            out.push_back(static_cast<char>(std::tolower(c)));
            last_space = false;
        } else if (!last_space) {
            out.push_back(' ');
            last_space = true;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::set<std::string> qaDuplicateTokens(const std::string& value) {
    std::set<std::string> tokens;
    std::istringstream in(normalizeQaDuplicateText(value));
    std::string token;
    while (in >> token) {
        if (token.size() >= 3) {
            tokens.insert(token);
        }
    }
    return tokens;
}

double qaTokenJaccard(const std::string& a, const std::string& b) {
    const auto ta = qaDuplicateTokens(a);
    const auto tb = qaDuplicateTokens(b);
    if (ta.empty() || tb.empty()) {
        return 0.0;
    }
    size_t inter = 0;
    for (const auto& token : ta) {
        if (tb.count(token)) {
            ++inter;
        }
    }
    const size_t uni = ta.size() + tb.size() - inter;
    return uni == 0 ? 0.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

boost::json::object qaDuplicateCandidateObject(const QASource::QAPair& pair,
                                               double similarity,
                                               const std::string& reason) {
    boost::json::object obj;
    obj["pair_id"] = pair.id;
    obj["question"] = pair.question;
    obj["category"] = pair.category.empty() ? "general" : pair.category;
    obj["similarity"] = similarity;
    obj["reason"] = reason;
    obj["source_type"] = "qa";
    obj["path"] = qaContextPath(pair);
    return obj;
}

std::string urlDecode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+' ) {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long code = std::strtol(std::string(hex).c_str(), &end, 16);
            if (end && *end == '\0') {
                decoded.push_back(static_cast<char>(code));
                i += 2;
            } else {
                decoded.push_back(value[i]);
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::map<std::string, std::string> parseQueryParams(const urls::url_view& url_view) {
    std::map<std::string, std::string> params;
    std::string query = url_view.query();
    if (!query.empty() && query[0] == '?') {
        query = query.substr(1);
    }
    if (query.empty()) {
        return params;
    }
    std::stringstream ss(query);
    std::string param;
    while (std::getline(ss, param, '&')) {
        const auto eq_pos = param.find('=');
        if (eq_pos == std::string::npos) {
            params[urlDecode(param)] = "";
        } else {
            params[urlDecode(std::string_view(param).substr(0, eq_pos))] =
                urlDecode(std::string_view(param).substr(eq_pos + 1));
        }
    }
    return params;
}

size_t querySize(const std::map<std::string, std::string>& params,
                 const std::string& key,
                 size_t fallback,
                 size_t max_value) {
    auto it = params.find(key);
    if (it == params.end() || it->second.empty()) {
        return fallback;
    }
    try {
        const size_t value = static_cast<size_t>(std::stoull(it->second));
        if (value == 0) {
            return fallback;
        }
        return std::min(value, max_value);
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace

namespace {

bool hasRagInterfaceTemplate(const std::filesystem::path &dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / "rag_interface.html", ec);
}

std::string resolveRagTemplatesDir() {
    std::vector<std::filesystem::path> candidates;

    if (const char *env_dir = std::getenv("QORNIX_RAG_TEMPLATES_DIR")) {
        if (*env_dir != '\0') {
            candidates.emplace_back(env_dir);
        }
    }

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        candidates.emplace_back(cwd / "qornix_rag" / "templates");
        candidates.emplace_back(cwd / "templates");
    }

    for (const auto &candidate : candidates) {
        if (hasRagInterfaceTemplate(candidate)) {
            return candidate.string();
        }
    }

    if (!candidates.empty()) {
        std::cerr << "Warning: RAG UI template directory was not resolved from candidates:" << std::endl;
        for (const auto &candidate : candidates) {
            std::cerr << "  - " << candidate.string() << std::endl;
        }
    }

    return "templates";
}

} // namespace

// ============================================================================
// RagApiHandler constructor
// ============================================================================

RagApiHandler::RagApiHandler(std::shared_ptr<RagService> service)
    : rag_service_(std::move(service)),
      rag_engine_(rag_service_ ? rag_service_->engine() : nullptr),
      llm_client_(rag_service_ ? rag_service_->llm() : nullptr) {}

RagApiHandler::RagApiHandler(
    std::shared_ptr<RagEngine> engine,
    std::shared_ptr<LLMClient> llm,
    std::shared_ptr<ICache> cache,
    std::shared_ptr<RateLimiter> limiter,
    std::shared_ptr<BatchProcessor> batch,
    std::shared_ptr<IPromptCache> pcache,
    std::shared_ptr<LLMRAGMetrics> metrics,
    std::shared_ptr<AnalyticsService> analytics,
    std::shared_ptr<MarkdownSource> markdown,
    std::shared_ptr<DeduplicationService> dedup
#if QORNIX_HAS_SQLITE
    , std::shared_ptr<SQLiteSource> sqlite
#endif
    )
#if QORNIX_HAS_SQLITE
    : rag_engine_(std::move(engine)),
      llm_client_(std::move(llm)),
      cache_(std::move(cache)),
      rate_limiter_(std::move(limiter)),
      batch_processor_(std::move(batch)),
      prompt_cache_(std::move(pcache)),
      metrics_(std::move(metrics)),
      analytics_service_(std::move(analytics)),
      markdown_source_(std::move(markdown)),
      dedup_service_(std::move(dedup)),
      sqlite_source_(std::move(sqlite)) {
        rag_service_ = std::make_shared<RagService>(rag_engine_, llm_client_, sqlite_source_);
      }
#else
    : rag_engine_(std::move(engine)),
      llm_client_(std::move(llm)),
      cache_(std::move(cache)),
      rate_limiter_(std::move(limiter)),
      batch_processor_(std::move(batch)),
      prompt_cache_(std::move(pcache)),
      metrics_(std::move(metrics)),
      analytics_service_(std::move(analytics)),
      markdown_source_(std::move(markdown)),
      dedup_service_(std::move(dedup)) {
        rag_service_ = std::make_shared<RagService>(rag_engine_, llm_client_);
      }
#endif

RagApiHandler::RagApiHandler(std::shared_ptr<RagEngine> engine)
    : rag_engine_(std::move(engine)) {
    rag_service_ = std::make_shared<RagService>(rag_engine_);
}

void RagApiHandler::setAuthOptions(const RagRouteAuthOptions& options) {
    auth_options_ = options;
}

void RagApiHandler::setUploadOptions(const qornix::rag::RagUploadConfig& options) {
    upload_options_ = options;
}

bool RagApiHandler::authorizeRequest(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::string& method
) const {
    if (!auth_options_.enabled) {
        return true;
    }

    const std::string path = url_view.path();
    const bool needs_admin = auth_options_.protect_admin_routes && isAdminRoute(path);
    const bool needs_write = auth_options_.protect_write_routes && isWriteRoute(path, method);
    if (!needs_admin && !needs_write) {
        return true;
    }

    if (auth_options_.mode == "host_header") {
        const std::string role = headerValue(req, auth_options_.role_header);
        if (!role.empty() && role == auth_options_.admin_role) {
            return true;
        }
        buildErrorResponse(res, http::status::forbidden, "RAG admin permission required");
        return false;
    }

    const std::string expected = configuredAdminToken(auth_options_);
    if (expected.empty()) {
        buildErrorResponse(res, http::status::service_unavailable, "RAG admin token is not configured");
        return false;
    }

    const std::string header_token = headerValue(req, auth_options_.token_header);
    const std::string auth_token = bearerToken(headerValue(req, "Authorization"));
    if (header_token == expected || auth_token == expected) {
        return true;
    }

    buildErrorResponse(res, http::status::unauthorized, "RAG admin token required");
    res.set(http::field::www_authenticate, "Bearer realm=\"qornix-rag-admin\"");
    return false;
}

// ============================================================================
// RagWebHandler constructor
// ============================================================================

RagWebHandler::RagWebHandler(const std::string &templates_dir,
                             const std::string &api_base)
    : templates_dir_(templates_dir),
      api_base_(api_base) {}

// ============================================================================
// RagApiHandler::handlePost
// ============================================================================

void RagApiHandler::handlePost(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &) {
    try {
        if (!authorizeRequest(req, res, url_view, "POST")) {
            return;
        }
        // Determine request type by URL
        std::string path = url_view.path();

        if (apiPathMatches(path, "/api/index")) {
            // Filesystem scan indexing
            boost::json::value json_req = boost::json::parse(req.body());
            std::optional<std::string> scan_path;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("scan_path")) {
                    std::string requested_path = obj.at("scan_path").as_string().c_str();
                    if (!requested_path.empty() && requested_path != "." && requested_path != "./") {
                        scan_path = requested_path;
                    }
                }
            }

            auto indexed = rag_service_->indexProject(scan_path);

            // Build response
            boost::json::object response;
            response["success"] = indexed.success;
            response["message"] = indexed.message;
            response["stats"] = {
                {"total_files", indexed.stats.total_files},
                {"total_lines", indexed.stats.total_lines},
                {"total_size_kb", indexed.stats.total_size_bytes / 1024},
                {"indexed_chunks", indexed.stats.indexed_chunks},
                {"reused_embeddings", indexed.stats.reused_embeddings},
                {"generated_embeddings", indexed.stats.generated_embeddings},
                {"stale_embeddings", indexed.stats.stale_embeddings},
                {"index_duration_ms", static_cast<std::int64_t>(indexed.stats.index_duration_ms)}
            };

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/documents/upload")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }
            RagUploadService upload_service(upload_options_);
            if (!upload_service.config().enabled) {
                buildErrorResponse(res, http::status::forbidden, "Document uploads are disabled");
                return;
            }

            const std::string content_type = headerValue(req, "Content-Type");
            auto parsed = upload_service.parseMultipart(content_type, req.body());
            if (!parsed.ok) {
                buildErrorResponse(res, http::status::bad_request, parsed.error.empty() ? "Invalid multipart upload" : parsed.error);
                return;
            }
            if (parsed.files.empty()) {
                buildErrorResponse(res, http::status::bad_request, "No files were provided in multipart form field 'files'");
                return;
            }
            if (upload_service.config().max_files_per_request > 0 &&
                parsed.files.size() > upload_service.config().max_files_per_request) {
                buildErrorResponse(res, http::status::payload_too_large, "Too many files in upload request");
                return;
            }

            boost::json::array validation_array;
            for (const auto& file : parsed.files) {
                const auto validation = upload_service.validateFile(file.filename, file.content_type, file.content.size());
                boost::json::object validation_obj;
                validation_obj["filename"] = file.filename;
                validation_obj["ok"] = validation.ok;
                validation_obj["code"] = validation.code;
                validation_obj["message"] = validation.message;
                validation_obj["sanitized_filename"] = validation.sanitized_filename;
                validation_obj["extension"] = validation.extension;
                validation_obj["content_type"] = validation.normalized_mime;
                validation_obj["size_bytes"] = static_cast<std::int64_t>(file.content.size());
                validation_array.emplace_back(validation_obj);
                if (!validation.ok) {
                    boost::json::object response;
                    response["success"] = false;
                    response["error"] = validation.message;
                    response["validation"] = validation_array;
                    buildJsonResponse(res, http::status::bad_request, boost::json::serialize(response));
                    return;
                }
            }

            const auto query_params = parseQueryParams(url_view);
            bool auto_ingest = upload_service.config().auto_ingest;
            bool async_ingest = upload_service.config().async_ingest;
            auto field_it = parsed.fields.find("auto_ingest");
            if (field_it != parsed.fields.end()) {
                auto_ingest = uploadStringBool(field_it->second, auto_ingest);
            }
            field_it = parsed.fields.find("async");
            if (field_it != parsed.fields.end()) {
                async_ingest = uploadStringBool(field_it->second, async_ingest);
            }
            auto query_it = query_params.find("auto_ingest");
            if (query_it != query_params.end()) {
                auto_ingest = uploadStringBool(query_it->second, auto_ingest);
            }
            query_it = query_params.find("async");
            if (query_it != query_params.end()) {
                async_ingest = uploadStringBool(query_it->second, async_ingest);
            }

            std::string batch_id;
            std::vector<RagStoredUpload> stored_files;
            try {
                stored_files = upload_service.storeFiles(parsed.files, &batch_id);
            } catch (const std::exception& e) {
                buildErrorResponse(res, http::status::bad_request, e.what());
                return;
            }

            boost::json::array files_array;
            for (const auto& file : stored_files) {
                files_array.emplace_back(storedUploadObject(file));
            }

            boost::json::object response;
            response["success"] = true;
            response["message"] = "Files uploaded";
            response["batch_id"] = batch_id;
            response["uploads_dir"] = upload_service.config().uploads_dir;
            response["files"] = files_array;
            response["count"] = static_cast<std::int64_t>(stored_files.size());
            response["validation"] = validation_array;
            response["auto_ingest"] = auto_ingest;
            response["async"] = async_ingest;

            if (auto_ingest) {
                std::optional<std::string> upload_scan_path;
                if (rag_service_->engine() && rag_service_->engine()->get_indexed_project_root().empty()) {
                    upload_scan_path = upload_service.config().uploads_dir;
                }
                auto ingested = async_ingest
                    ? rag_service_->startBackgroundIngestProject(upload_scan_path)
                    : rag_service_->ingestProject(upload_scan_path);
                response["ingestion_started"] = true;
                response["job"] = ingestionJobObject(ingested.job);
                response["ingestion_success"] = ingested.success;
                response["ingestion_message"] = ingested.message;
                if (!async_ingest) {
                    response["stats"] = {
                        {"total_files", ingested.stats.total_files},
                        {"total_lines", ingested.stats.total_lines},
                        {"total_size_kb", ingested.stats.total_size_bytes / 1024},
                        {"indexed_chunks", ingested.stats.indexed_chunks},
                        {"reused_embeddings", ingested.stats.reused_embeddings},
                        {"generated_embeddings", ingested.stats.generated_embeddings},
                        {"stale_embeddings", ingested.stats.stale_embeddings},
                        {"index_duration_ms", static_cast<std::int64_t>(ingested.stats.index_duration_ms)}
                    };
                }
                buildJsonResponse(res, async_ingest ? http::status::accepted : http::status::ok,
                                  boost::json::serialize(response));
                return;
            }

            response["ingestion_started"] = false;
            buildJsonResponse(res, http::status::created, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/uploads/delete")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }
            boost::json::value json_req = req.body().empty()
                ? boost::json::object{}
                : boost::json::parse(req.body());
            std::string relative_path;
            bool reindex = true;
            if (json_req.if_object()) {
                const auto& obj = json_req.as_object();
                if (obj.contains("relative_path") && obj.at("relative_path").is_string()) {
                    relative_path = obj.at("relative_path").as_string().c_str();
                } else if (obj.contains("path") && obj.at("path").is_string()) {
                    relative_path = obj.at("path").as_string().c_str();
                }
                if (obj.contains("reindex") && obj.at("reindex").is_bool()) {
                    reindex = obj.at("reindex").as_bool();
                }
            }
            if (relative_path.empty()) {
                buildErrorResponse(res, http::status::bad_request, "relative_path is required");
                return;
            }

            RagUploadService upload_service(upload_options_);
            std::string removed_abs;
            std::string removed_rel;
            if (!upload_service.removeStoredFile(relative_path, &removed_abs, &removed_rel)) {
                buildErrorResponse(res, http::status::not_found, "Uploaded file not found or outside uploads_dir");
                return;
            }

            const bool persisted_deleted = rag_service_->deletePersistedDocument(removed_rel);
            boost::json::object response;
            response["success"] = true;
            response["message"] = "Uploaded document deleted";
            response["relative_path"] = removed_rel;
            response["path"] = removed_abs;
            response["persisted_deleted"] = persisted_deleted;
            response["reindex"] = reindex;
            if (reindex) {
                std::optional<std::string> upload_scan_path;
                if (rag_service_->engine() && rag_service_->engine()->get_indexed_project_root().empty()) {
                    upload_scan_path = upload_service.config().uploads_dir;
                }
                auto indexed = rag_service_->indexProject(upload_scan_path);
                response["reindex_success"] = indexed.success;
                response["reindex_message"] = indexed.message;
                response["stats"] = {
                    {"total_files", indexed.stats.total_files},
                    {"total_lines", indexed.stats.total_lines},
                    {"total_size_kb", indexed.stats.total_size_bytes / 1024},
                    {"indexed_chunks", indexed.stats.indexed_chunks},
                    {"reused_embeddings", indexed.stats.reused_embeddings},
                    {"generated_embeddings", indexed.stats.generated_embeddings},
                    {"stale_embeddings", indexed.stats.stale_embeddings},
                    {"index_duration_ms", static_cast<std::int64_t>(indexed.stats.index_duration_ms)}
                };
            }
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/ingest")) {
            boost::json::value json_req = req.body().empty()
                ? boost::json::object{}
                : boost::json::parse(req.body());
            std::optional<std::string> scan_path;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("scan_path")) {
                    std::string requested_path = obj.at("scan_path").as_string().c_str();
                    if (!requested_path.empty() && requested_path != "." && requested_path != "./") {
                        scan_path = requested_path;
                    }
                }
                bool background = false;
                if (obj.contains("async") && obj.at("async").is_bool()) {
                    background = obj.at("async").as_bool();
                }
                if (obj.contains("background") && obj.at("background").is_bool()) {
                    background = obj.at("background").as_bool();
                }
                if (background) {
                    auto queued = rag_service_->startBackgroundIngestProject(scan_path);
                    boost::json::object response;
                    response["success"] = queued.success;
                    response["message"] = queued.message;
                    response["job"] = ingestionJobObject(queued.job);
                    buildJsonResponse(res, queued.success ? http::status::accepted : http::status::internal_server_error,
                                      boost::json::serialize(response));
                    return;
                }
            }

            auto ingested = rag_service_->ingestProject(scan_path);
            boost::json::object response;
            response["success"] = ingested.success;
            response["message"] = ingested.message;
            response["job"] = ingestionJobObject(ingested.job);
            response["stats"] = {
                {"total_files", ingested.stats.total_files},
                {"total_lines", ingested.stats.total_lines},
                {"total_size_kb", ingested.stats.total_size_bytes / 1024},
                {"indexed_chunks", ingested.stats.indexed_chunks},
                {"reused_embeddings", ingested.stats.reused_embeddings},
                {"generated_embeddings", ingested.stats.generated_embeddings},
                {"stale_embeddings", ingested.stats.stale_embeddings},
                {"index_duration_ms", static_cast<std::int64_t>(ingested.stats.index_duration_ms)}
            };

            buildJsonResponse(res, ingested.success ? http::status::ok : http::status::internal_server_error,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/embedding/switch")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }

            boost::json::value json_req = req.body().empty()
                ? boost::json::object{}
                : boost::json::parse(req.body());
            std::string model_id;
            std::optional<std::string> scan_path;
            bool reindex = false;
            bool force_reembed = true;

            if (json_req.if_object()) {
                const auto& obj = json_req.as_object();
                if (obj.contains("model_id")) {
                    model_id = obj.at("model_id").as_string().c_str();
                }
                if (obj.contains("scan_path")) {
                    std::string requested_path = obj.at("scan_path").as_string().c_str();
                    if (!requested_path.empty() && requested_path != "." && requested_path != "./") {
                        scan_path = requested_path;
                    }
                }
                if (obj.contains("reindex") && obj.at("reindex").is_bool()) {
                    reindex = obj.at("reindex").as_bool();
                }
                if (obj.contains("force_reembed") && obj.at("force_reembed").is_bool()) {
                    force_reembed = obj.at("force_reembed").as_bool();
                }
            }

            auto switched = rag_service_->switchEmbeddingModel(model_id, reindex, force_reembed, scan_path);
            boost::json::object response;
            response["success"] = switched.success;
            response["message"] = switched.message;
            response["previous_model_id"] = switched.previous_model_id;
            response["active_model_id"] = switched.active_model_id;
            response["effective_model_id"] = switched.effective_model_id;
            response["reindex_required"] = switched.reindex_required;
            response["reindexed"] = switched.reindexed;
            response["force_reembed"] = switched.force_reembed;
            response["stats"] = {
                {"total_files", switched.stats.total_files},
                {"total_lines", switched.stats.total_lines},
                {"total_size_kb", switched.stats.total_size_bytes / 1024},
                {"indexed_chunks", switched.stats.indexed_chunks},
                {"reused_embeddings", switched.stats.reused_embeddings},
                {"generated_embeddings", switched.stats.generated_embeddings},
                {"stale_embeddings", switched.stats.stale_embeddings},
                {"index_duration_ms", static_cast<std::int64_t>(switched.stats.index_duration_ms)}
            };
            buildJsonResponse(res, switched.success ? http::status::ok : http::status::bad_request,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/documents/delete")) {
            boost::json::value json_req = boost::json::parse(req.body());
            std::string relative_path;
            std::string source_id;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("relative_path")) {
                    relative_path = obj.at("relative_path").as_string().c_str();
                }
                if (obj.contains("source_id")) {
                    source_id = obj.at("source_id").as_string().c_str();
                }
            }

            if (relative_path.empty()) {
                buildErrorResponse(res, http::status::bad_request, "relative_path is required");
                return;
            }

            if (!rag_service_->deletePersistedDocument(relative_path, source_id)) {
                buildErrorResponse(res, http::status::not_found, "Persisted document not found: " + relative_path);
                return;
            }

            boost::json::object response;
            response["success"] = true;
            response["message"] = "Persisted document deleted";
            response["relative_path"] = relative_path;
            if (!source_id.empty()) {
                response["source_id"] = source_id;
            }
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/search")) {
            // Project search
            boost::json::value json_req = boost::json::parse(req.body());
            std::string query;
            size_t top_k = 10;
            [[maybe_unused]] bool full_context = false;
            std::vector<MetadataFilter> metadata_filters;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                metadata_filters = parseMetadataFilters(obj);
                if (obj.contains("query")) {
                    query = obj.at("query").as_string().c_str();
                }
                if (obj.contains("top_k")) {
                    top_k = static_cast<size_t>(obj.at("top_k").as_int64());
                }
                if (obj.contains("full_context")) {
                    full_context = obj.at("full_context").as_bool();
                }
            }

            if (query.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Query is required");
                return;
            }

            auto search_response = rag_service_->search(query, top_k, metadata_filters);

            // Phase 5: Log search to analytics
            if (analytics_service_) {
                AnalyticsService::SearchQuery log_entry;
                log_entry.query = query;
                log_entry.timestamp = std::chrono::system_clock::now();
                log_entry.result_count = search_response.results.size();
                log_entry.has_answer = !search_response.results.empty();
                log_entry.response_time_ms = static_cast<int>(search_response.response_time_ms);

                // Extract client IP
                auto it_ip = req.find("X-Forwarded-For");
                if (it_ip != req.end()) {
                    log_entry.client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                } else {
                    it_ip = req.find("X-Real-IP");
                    if (it_ip != req.end()) {
                        log_entry.client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                    }
                }

                if (!search_response.results.empty()) {
                    log_entry.top_result_path = search_response.results[0].path;
                }

                analytics_service_->logSearch(log_entry);
            }

            // Build response
            boost::json::array results_array;
            for (const auto &result: search_response.results) {
                boost::json::object result_obj{
                    {"path", result.path},
                    {"source_path", result.source_path},
                    {"citation_id", result.citation_id},
                    {"type", result.type},
                    {"language", result.language},
                    {"score", result.score},
                    {"confidence", result.confidence},
                    {"vector_score", result.vector_score},
                    {"text_score", result.text_score},
                    {"fused_score", result.fused_score},
                    {"snippet", result.snippet},
                    {"lines", static_cast<std::int64_t>(result.lines)},
                    {"size", static_cast<std::int64_t>(result.size)},
                    {"source_type", result.source_type}
                };
                if (!result.source_locator.empty()) result_obj["source_locator"] = result.source_locator;
                if (!result.citation_label.empty()) result_obj["citation_label"] = result.citation_label;
                if (!result.metadata.empty()) result_obj["metadata"] = stringMapJson(result.metadata);
                if (!result.category.empty()) result_obj["category"] = result.category;
                if (!result.pair_id.empty()) result_obj["pair_id"] = result.pair_id;
                if (!result.tags.empty()) result_obj["tags"] = stringArrayJson(result.tags);
                if (!result.attribution.empty()) result_obj["attribution"] = result.attribution;
                results_array.emplace_back(result_obj);
            }

            boost::json::object response;
            response["success"] = search_response.success;
            response["query"] = query;
            response["expanded_query"] = search_response.expanded_query;
            response["rewritten_query"] = search_response.rewritten_query;
            response["query_expansion_applied"] = search_response.query_expansion_applied;
            response["multi_query_applied"] = search_response.multi_query_applied;
            response["retrieval_strategy"] = search_response.retrieval_strategy;
            response["retrieval_queries"] = retrievalQueriesJson(search_response.retrieval_queries);
            response["reranking_applied"] = search_response.reranking_applied;
            response["reranker_type"] = search_response.reranker_type;
            response["filters_applied"] = search_response.filters_applied;
            response["filters"] = metadataFiltersJson(search_response.filters);
            response["results"] = results_array;
            response["count"] = static_cast<std::int64_t>(results_array.size());

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/ask")) {
            // LLM-powered Q&A with RAG context
            boost::json::value json_req = boost::json::parse(req.body());
            std::string question;
            std::optional<std::string> system_prompt;
            std::optional<std::string> prompt_template;
            size_t top_k = 5;
            std::vector<MetadataFilter> metadata_filters;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                metadata_filters = parseMetadataFilters(obj);
                if (obj.contains("question")) {
                    question = obj.at("question").as_string().c_str();
                }
                if (obj.contains("system_prompt") && obj.at("system_prompt").is_string()) {
                    std::string value = obj.at("system_prompt").as_string().c_str();
                    if (!value.empty()) {
                        system_prompt = value;
                    }
                }
                if (obj.contains("prompt_template") && obj.at("prompt_template").is_string()) {
                    std::string value = obj.at("prompt_template").as_string().c_str();
                    if (!value.empty()) {
                        prompt_template = value;
                    }
                }
                if (obj.contains("top_k")) {
                    top_k = static_cast<size_t>(obj.at("top_k").as_int64());
                }
            }

            if (question.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Question is required");
                return;
            }

            bool do_stream = false;
            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("stream")) {
                    do_stream = obj.at("stream").as_bool();
                }
            }

            if (!do_stream) {
                std::string client_ip;
                auto it_ip = req.find("X-Forwarded-For");
                if (it_ip != req.end()) {
                    client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                } else {
                    it_ip = req.find("X-Real-IP");
                    if (it_ip != req.end()) {
                        client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                    }
                }

                std::vector<RagServiceConversationTurn> history;
                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("history") && obj.at("history").is_array()) {
                        for (const auto& value : obj.at("history").as_array()) {
                            if (!value.is_object()) {
                                continue;
                            }
                            const auto& turn_obj = value.as_object();
                            RagServiceConversationTurn turn;
                            if (turn_obj.contains("role") && turn_obj.at("role").is_string()) {
                                turn.role = turn_obj.at("role").as_string().c_str();
                            }
                            if (turn_obj.contains("content") && turn_obj.at("content").is_string()) {
                                turn.content = turn_obj.at("content").as_string().c_str();
                            }
                            if (!turn.role.empty() && !turn.content.empty()) {
                                history.push_back(std::move(turn));
                            }
                        }
                    }
                }

                auto ask_response = rag_service_->ask(
                    question,
                    top_k,
                    client_ip,
                    history,
                    metadata_filters,
                    system_prompt,
                    prompt_template);
                boost::json::array context_array;
                boost::json::array sources_array;
                boost::json::array citations_array;
                boost::json::array answer_citations_array;
                boost::json::array missing_citations_array;
                boost::json::array uncited_context_citations_array;

                for (const auto& item : ask_response.context) {
                    boost::json::object ctx_obj{
                        {"path", item.path},
                        {"source_path", item.source_path},
                        {"citation_id", item.citation_id},
                        {"score", item.score},
                        {"confidence", item.confidence},
                        {"snippet", item.snippet},
                        {"source_type", item.source_type}
                    };
                    if (!item.source_locator.empty()) ctx_obj["source_locator"] = item.source_locator;
                    if (!item.citation_label.empty()) ctx_obj["citation_label"] = item.citation_label;
                    if (!item.metadata.empty()) ctx_obj["metadata"] = stringMapJson(item.metadata);
                    if (!item.category.empty()) ctx_obj["category"] = item.category;
                    if (!item.pair_id.empty()) ctx_obj["pair_id"] = item.pair_id;
                    if (!item.tags.empty()) ctx_obj["tags"] = stringArrayJson(item.tags);
                    if (!item.attribution.empty()) ctx_obj["attribution"] = item.attribution;
                    context_array.emplace_back(ctx_obj);
                }
                for (const auto& source : ask_response.sources) {
                    sources_array.emplace_back(source);
                }
                for (const auto& citation : ask_response.citations) {
                    citations_array.emplace_back(citation);
                }
                for (const auto& citation : ask_response.answer_citations) {
                    answer_citations_array.emplace_back(citation);
                }
                for (const auto& citation : ask_response.missing_citations) {
                    missing_citations_array.emplace_back(citation);
                }
                for (const auto& citation : ask_response.uncited_context_citations) {
                    uncited_context_citations_array.emplace_back(citation);
                }

                boost::json::object response;
                response["success"] = ask_response.success;
                response["question"] = ask_response.question;
                response["expanded_query"] = ask_response.expanded_query;
                response["rewritten_query"] = ask_response.rewritten_query;
                response["query_expansion_applied"] = ask_response.query_expansion_applied;
                response["multi_query_applied"] = ask_response.multi_query_applied;
                response["retrieval_strategy"] = ask_response.retrieval_strategy;
                response["retrieval_queries"] = retrievalQueriesJson(ask_response.retrieval_queries);
                response["reranking_applied"] = ask_response.reranking_applied;
                response["reranker_type"] = ask_response.reranker_type;
                response["filters_applied"] = ask_response.filters_applied;
                response["filters"] = metadataFiltersJson(ask_response.filters);
                response["context"] = context_array;
                response["sources"] = sources_array;
                response["citations"] = citations_array;
                response["answer_citations"] = answer_citations_array;
                response["missing_citations"] = missing_citations_array;
                response["uncited_context_citations"] = uncited_context_citations_array;
                response["citations_post_processed"] = ask_response.citations_post_processed;
                response["retrieval_confidence"] = ask_response.retrieval_confidence;
                response["grounding_status"] = ask_response.grounding_status;
                response["grounding_evaluator"] = ask_response.grounding_evaluator;
                response["claim_grounding_status"] = ask_response.claim_grounding_status;
                response["grounded_claims"] = groundingClaimsJson(ask_response.grounded_claims);
                response["conversation_turns_used"] = static_cast<std::int64_t>(ask_response.conversation_turns_used);
                response["answer"] = ask_response.answer;
                response["llm_status"] = ask_response.llm_status;
                response["llm_truncated"] = ask_response.llm_truncated;
                response["llm_finish_reason"] = ask_response.llm_finish_reason;
                response["llm_parser_error"] = ask_response.llm_parser_error;
                response["response_time_ms"] = static_cast<std::int64_t>(ask_response.response_time_ms);

                if (cache_) {
                    auto stats = llm_client_->get_cache_stats();
                    boost::json::object cache_obj;
                    cache_obj["enabled"] = true;
                    cache_obj["backend"] = cache_->backend_name();
                    cache_obj["available"] = cache_->is_available();
                    cache_obj["hits"] = static_cast<std::int64_t>(stats.hits);
                    cache_obj["misses"] = static_cast<std::int64_t>(stats.misses);
                    cache_obj["size"] = static_cast<std::int64_t>(stats.size);
                    cache_obj["max_size"] = static_cast<std::int64_t>(stats.max_size);
                    cache_obj["hit_rate_percent"] = static_cast<double>(stats.hit_rate());
                    response["cache"] = cache_obj;
                }

                if (rate_limiter_) {
                    auto rl_stats = llm_client_->get_rate_limiter_stats();
                    boost::json::object rl_obj;
                    rl_obj["enabled"] = true;
                    rl_obj["allowed"] = static_cast<std::int64_t>(rl_stats.allowed);
                    rl_obj["rejected"] = static_cast<std::int64_t>(rl_stats.rejected);
                    rl_obj["rejection_rate_percent"] = static_cast<double>(rl_stats.rejection_rate());
                    response["rate_limiter"] = rl_obj;
                }

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
                return;
            }

            // Search for context
            auto results = rag_engine_->search(question, top_k);

            // Build context string. User-maintained QA pairs are added first
            // so local wiki knowledge has priority over generic project snippets.
            std::string context;
            boost::json::array context_array;
            boost::json::array sources_array;

#if QORNIX_HAS_SQLITE
            if (sqlite_source_) {
                const auto qa_pairs = sqlite_source_->searchByQuestion(question);
                const size_t qa_limit = std::min<size_t>(qa_pairs.size(), std::min<size_t>(top_k, 5));
                for (size_t i = 0; i < qa_limit; ++i) {
                    const auto& pair = qa_pairs[i];
                    if (!context.empty()) {
                        context += "\n---\n";
                    }
                    context += "[QA Knowledge Base] " + qaContextPath(pair) + "\n" + qaContextSnippet(pair);
                    context_array.emplace_back(qaContextObject(pair, 1.0 - (static_cast<double>(i) * 0.01)));
                    sources_array.emplace_back(qaContextPath(pair));
                }
            }
#endif

            for (const auto &result: results) {
                std::ostringstream ctx;
                ctx << result.document.relative_path << ": " << result.snippet;
                if (!context.empty()) {
                    context += "\n---\n";
                }
                context += ctx.str();

                boost::json::object ctx_obj{
                    {"path", result.document.relative_path},
                    {"score", result.fused_score},
                    {"snippet", result.snippet},
                    {"source_type", "project"}
                };
                context_array.emplace_back(ctx_obj);
                sources_array.emplace_back(result.document.relative_path);
            }

            boost::json::object response;
            response["success"] = true;
            response["question"] = question;
            response["context"] = context_array;
            response["sources"] = sources_array;

            if (llm_client_ && llm_client_->is_enabled()) {
                if (do_stream) {
                    // SSE Streaming response
                    res.set("Content-Type", "text/event-stream; charset=utf-8");
                    res.set("Cache-Control", "no-cache");
                    res.set("Connection", "keep-alive");
                    res.set("Access-Control-Allow-Origin", "*");

                    std::string response_body;

                    bool success = llm_client_->ask_stream_sse(
                        question, context,
                        [&response_body](const std::string& chunk, bool is_done) {
                            if (!chunk.empty()) {
                                response_body += "data: {"
                                    "\"chunk\": \"" + LLMClient::json_escape_chunk(chunk) + "\""
                                    ", \"done\": " + (is_done ? "true" : "false")
                                    + "}\n\n";
                            } else if (is_done) {
                                response_body += "data: {\"done\": true}\n\n";
                            }
                        }
                    );

                    res.body() = success ? response_body : "data: {\"error\": \"Streaming failed\"}\n\n";
                    res.result(http::status::ok);
                    return;
                }

                // Non-streaming response
                auto start_time = std::chrono::steady_clock::now();
                std::string answer;

                // Extract client IP from request headers (X-Forwarded-For, X-Real-IP, or remote address)
                std::string client_ip;
                auto it_ip = req.find("X-Forwarded-For");
                if (it_ip != req.end()) {
                    client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                } else {
                    it_ip = req.find("X-Real-IP");
                    if (it_ip != req.end()) {
                        client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                    }
                }

                const auto llm_result = llm_client_->ask_with_metadata(question, context, client_ip);
                answer = llm_result.answer;
                auto end_time = std::chrono::steady_clock::now();

                long long response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();

                const bool llm_fallback_answer =
                    answer.rfind("LLM unavailable", 0) == 0 ||
                    answer.rfind("LLM is not configured", 0) == 0 ||
                    answer.rfind("Error:", 0) == 0;

                response["answer"] = answer;
                if (llm_result.status == "parser_error" ||
                    llm_result.status == "provider_error" ||
                    llm_result.status == "truncated" ||
                    llm_result.status == "rate_limited" ||
                    llm_result.status == "cache_hit" ||
                    llm_result.status == "unavailable") {
                    response["llm_status"] = llm_result.status;
                } else {
                    response["llm_status"] = llm_fallback_answer
                        ? "fallback"
                        : (llm_client_->is_available() ? "ok" : "unavailable");
                }
                response["llm_truncated"] = llm_result.truncated;
                response["llm_finish_reason"] = llm_result.finish_reason;
                response["llm_parser_error"] = llm_result.parser_error;
                response["response_time_ms"] = static_cast<std::int64_t>(response_time);

                // Phase 3: Add cache stats
                if (cache_) {
                    auto stats = llm_client_->get_cache_stats();
                    boost::json::object cache_obj;
                    cache_obj["enabled"] = true;
                    cache_obj["backend"] = cache_->backend_name();
                    cache_obj["available"] = cache_->is_available();
                    cache_obj["hits"] = static_cast<std::int64_t>(stats.hits);
                    cache_obj["misses"] = static_cast<std::int64_t>(stats.misses);
                    cache_obj["size"] = static_cast<std::int64_t>(stats.size);
                    cache_obj["max_size"] = static_cast<std::int64_t>(stats.max_size);
                    cache_obj["hit_rate_percent"] = static_cast<double>(stats.hit_rate());
                    response["cache"] = cache_obj;
                }

                // Phase 3: Add rate limiter stats
                if (rate_limiter_) {
                    auto rl_stats = llm_client_->get_rate_limiter_stats();
                    boost::json::object rl_obj;
                    rl_obj["enabled"] = true;
                    rl_obj["allowed"] = static_cast<std::int64_t>(rl_stats.allowed);
                    rl_obj["rejected"] = static_cast<std::int64_t>(rl_stats.rejected);
                    rl_obj["rejection_rate_percent"] = static_cast<double>(rl_stats.rejection_rate());
                    response["rate_limiter"] = rl_obj;
                }
            } else {
                // LLM not available - return context only
                response["answer"] = "LLM unavailable. Relevant context fragments:\n" + context;
                response["llm_status"] = "unavailable";
                response["response_time_ms"] = 0;
            }

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        } else if (apiPathMatches(path, "/api/batch")) {
            // Batch processing of multiple questions
            boost::json::value json_req = boost::json::parse(req.body());
            std::vector<BatchQuestion> questions;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("questions")) {
                    const auto& q_array = obj.at("questions").as_array();
                    for (const auto& q_item : q_array) {
                        BatchQuestion q;
                        if (q_item.if_object()) {
                            auto q_obj = q_item.as_object();
                            if (q_obj.contains("question")) {
                                q.question = q_obj.at("question").as_string().c_str();
                            }
                            if (q_obj.contains("context")) {
                                q.context = q_obj.at("context").as_string().c_str();
                            }
                            if (q_obj.contains("top_k")) {
                                q.top_k = static_cast<size_t>(q_obj.at("top_k").as_int64());
                            }
                            if (q_obj.contains("client_ip")) {
                                q.client_ip = q_obj.at("client_ip").as_string().c_str();
                            }
                            if (!q.question.empty()) {
                                questions.push_back(std::move(q));
                            }
                        }
                    }
                }
            }

            if (questions.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Questions array is required");
                return;
            }

            // Update metrics
            if (metrics_) {
                metrics_->inc_batch_questions(questions.size());
            }

            // Process batch
            auto start_time = std::chrono::steady_clock::now();

            std::vector<BatchResult> results;
            if (llm_client_ && llm_client_->is_enabled()) {
                results = batch_processor_->process(questions,
                    [this](const std::string& question, const std::string& context, const std::string& client_ip) -> std::string {
                        return llm_client_->ask(question, context, client_ip);
                    });
            } else {
                // LLM not available - return context only for each question
                results.resize(questions.size());
                for (size_t i = 0; i < questions.size(); i++) {
                    results[i].question = questions[i].question;
                    results[i].answer = "LLM unavailable. Relevant context fragments:\n" + questions[i].context;
                    results[i].success = false;
                    results[i].error = "LLM not available";
                }
            }

            auto end_time = std::chrono::steady_clock::now();
            long long batch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();

            // Update metrics
            if (metrics_) {
                size_t completed = 0;
                for (const auto& r : results) {
                    if (r.success) completed++;
                }
                metrics_->inc_batch_completed(completed);
                metrics_->observe_llm_request_duration_seconds(batch_duration / 1000.0);
            }

            // Build response
            boost::json::array results_array;
            for (const auto& r : results) {
                boost::json::object r_obj{
                    {"question", r.question},
                    {"success", r.success},
                    {"answer", r.answer},
                    {"response_time_ms", static_cast<std::int64_t>(r.response_time_ms)}
                };
                if (!r.error.empty()) {
                    r_obj["error"] = r.error;
                }
                results_array.emplace_back(r_obj);
            }

            auto stats = batch_processor_->get_stats();
            boost::json::object response;
            response["success"] = true;
            response["questions_count"] = static_cast<std::int64_t>(questions.size());
            response["results"] = results_array;
            response["batch_duration_ms"] = static_cast<std::int64_t>(batch_duration);
            response["stats"] = {
                {"total", static_cast<std::int64_t>(stats.total)},
                {"completed", static_cast<std::int64_t>(stats.completed)},
                {"failed", static_cast<std::int64_t>(stats.failed)},
                {"completion_rate_percent", static_cast<double>(stats.completion_rate())}
            };

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        } else if (apiPathMatches(path, "/api/metrics")) {
            // Prometheus metrics endpoint
            if (metrics_) {
                std::string metrics_text = metrics_->render_all();
                res.set("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
                res.body() = metrics_text;
                res.result(http::status::ok);
                res.prepare_payload();
            } else {
                buildErrorResponse(res, http::status::service_unavailable, "Metrics not enabled");
            }
        } else if (apiPathMatches(path, "/api/sources")) {
            // GET /api/sources - list data sources
            {
                auto sources = rag_engine_->getDataSources();
                boost::json::array sources_array;

                for (const auto& source : sources) {
                    boost::json::object source_obj{
                        {"id", source->getId()},
                        {"type", data_source_type_to_string(source->getType())},
                        {"name", source->getName()},
                        {"document_count", static_cast<std::int64_t>(source->count())}
                    };
                    sources_array.emplace_back(source_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["sources"] = sources_array;
                response["count"] = static_cast<std::int64_t>(sources.size());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/sources/add")) {
            // POST /api/sources/add - add a data source
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_type;
                std::string name;
                std::string source_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_type")) {
                        source_type = obj.at("source_type").as_string().c_str();
                    }
                    if (obj.contains("name")) {
                        name = obj.at("name").as_string().c_str();
                    }
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                }

                if (source_type.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "source_type is required");
                    return;
                }

                std::shared_ptr<DataSource> new_source;

                if (source_type == "qa_kb") {
                    // Create QASource
                    QASource::Config qa_config;
                    qa_config.name = name.empty() ? "API QA Source" : name;
                    qa_config.source_id = source_id.empty() ? ("qa_" + std::to_string(std::time(nullptr))) : source_id;

                    // Load initial pairs from JSON if provided
                    if (json_req.if_object()) {
                        auto obj = json_req.as_object();
                        if (obj.contains("pairs") && obj.at("pairs").is_array()) {
                            const auto& pairs_array = obj.at("pairs").as_array();
                            for (const auto& pair_item : pairs_array) {
                                if (pair_item.if_object()) {
                                    auto p = pair_item.as_object();
                                    if (p.contains("question") && p.contains("answer")) {
                                        QASource::QAPair qa_pair;
                                        qa_pair.question = p.at("question").as_string().c_str();
                                        qa_pair.answer = p.at("answer").as_string().c_str();
                                        if (p.contains("category")) {
                                            qa_pair.category = p.at("category").as_string().c_str();
                                        }
                                        if (p.contains("id")) {
                                            qa_pair.id = p.at("id").as_string().c_str();
                                        }
                                        qa_config.pairs.push_back(qa_pair);
                                    }
                                }
                            }
                        }
                    }

                    new_source = std::make_shared<QASource>(qa_config);
                } else if (source_type == "memory") {
                    // Create MemorySource
                    MemorySource::Config mem_config;
                    mem_config.name = name.empty() ? "API Memory Source" : name;
                    mem_config.allow_duplicates = false;

                    new_source = std::make_shared<MemorySource>(mem_config);
                } else {
                    buildErrorResponse(res, http::status::bad_request,
                                      "Unsupported source_type: " + source_type + ". Use 'qa_kb' or 'memory'");
                    return;
                }

                rag_engine_->addDataSource(new_source);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "Data source added";
                response["source_id"] = new_source->getId();
                response["source_type"] = data_source_type_to_string(new_source->getType());
                response["document_count"] = static_cast<std::int64_t>(new_source->count());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/sources/remove")) {
            // POST /api/sources/remove - remove a data source by ID
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                }

                if (source_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "source_id is required");
                    return;
                }

                rag_engine_->removeDataSource(source_id);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "Data source removed";
                response["source_id"] = source_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/qa")) {
            boost::json::value json_req = boost::json::parse(req.body());
            std::string question;
            std::string answer;
            std::string category = "general";
            std::vector<std::string> tags;
            std::vector<std::string> aliases;
            std::map<std::string, std::string> metadata;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("question")) {
                    question = obj.at("question").as_string().c_str();
                }
                if (obj.contains("answer")) {
                    answer = obj.at("answer").as_string().c_str();
                }
                if (obj.contains("category")) {
                    category = obj.at("category").as_string().c_str();
                }
                tags = jsonStringArray(obj, "tags");
                aliases = jsonStringArray(obj, "aliases");
                metadata = jsonStringMap(obj, "metadata");
            }

            std::string pair_id;
            const bool added = rag_service_ && rag_service_->addQaPair(question, answer, category, &pair_id, tags, aliases, metadata);
            if (!added) {
                buildErrorResponse(res, http::status::bad_request, "question and answer are required");
                return;
            }

            boost::json::object response;
            response["success"] = true;
            response["message"] = "QA pair added";
            response["pair_id"] = pair_id;
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
        } else if (apiPathMatches(path, "/api/qa/duplicate-check")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }

            boost::json::value json_req = req.body().empty() ? boost::json::object{} : boost::json::parse(req.body());
            std::string question;
            std::string answer;
            std::string category;
            std::string exclude_pair_id;
            double threshold = 0.72;
            size_t limit = 5;
            if (json_req.is_object()) {
                const auto& obj = json_req.as_object();
                if (obj.contains("question") && obj.at("question").is_string()) question = obj.at("question").as_string().c_str();
                if (obj.contains("answer") && obj.at("answer").is_string()) answer = obj.at("answer").as_string().c_str();
                if (obj.contains("category") && obj.at("category").is_string()) category = obj.at("category").as_string().c_str();
                if (obj.contains("exclude_pair_id") && obj.at("exclude_pair_id").is_string()) exclude_pair_id = obj.at("exclude_pair_id").as_string().c_str();
                if (obj.contains("threshold") && obj.at("threshold").is_double()) threshold = obj.at("threshold").as_double();
                if (obj.contains("limit") && obj.at("limit").is_int64()) limit = static_cast<size_t>(obj.at("limit").as_int64());
            }
            limit = std::min<size_t>(std::max<size_t>(limit, 1), 20);
            if (question.empty()) {
                buildErrorResponse(res, http::status::bad_request, "question is required");
                return;
            }

            std::vector<QASource::QAPair> pairs;
            RagServiceQaListOptions options;
            options.limit = 500;
            options.offset = 0;
            while (true) {
                auto page = rag_service_->listQaPairs(options);
                pairs.insert(pairs.end(), page.items.begin(), page.items.end());
                if (!page.has_more || page.items.empty()) {
                    break;
                }
                options.offset += page.items.size();
                if (pairs.size() >= 5000) {
                    break;
                }
            }

            struct Candidate {
                QASource::QAPair pair;
                double score;
                std::string reason;
            };
            std::vector<Candidate> candidates;
            const std::string normalized_question = normalizeQaDuplicateText(question);
            for (const auto& pair : pairs) {
                if (!exclude_pair_id.empty() && pair.id == exclude_pair_id) {
                    continue;
                }
                const std::string normalized_existing = normalizeQaDuplicateText(pair.question);
                double score = 0.0;
                std::string reason;
                if (!normalized_question.empty() && normalized_question == normalized_existing) {
                    score = 1.0;
                    reason = "exact normalized question match";
                } else {
                    for (const auto& alias : pair.aliases) {
                        if (normalizeQaDuplicateText(alias) == normalized_question) {
                            score = std::max(score, 0.96);
                            reason = "matches an existing alias";
                        }
                    }
                    const double token_score = qaTokenJaccard(question + " " + answer, pair.question + " " + pair.answer);
                    if (token_score > score) {
                        score = token_score;
                        reason = "high token overlap";
                    }
                    if (!category.empty() && category == pair.category && token_score >= threshold - 0.05) {
                        score = std::max(score, token_score + 0.05);
                        reason = "same category with high token overlap";
                    }
                }
                if (score >= threshold) {
                    candidates.push_back(Candidate{pair, std::min(score, 1.0), reason.empty() ? "similar QA entry" : reason});
                }
            }
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.score > b.score;
            });
            if (candidates.size() > limit) {
                candidates.resize(limit);
            }

            boost::json::array candidate_array;
            for (const auto& candidate : candidates) {
                candidate_array.emplace_back(qaDuplicateCandidateObject(candidate.pair, candidate.score, candidate.reason));
            }
            boost::json::object response;
            response["success"] = true;
            response["checked_pairs"] = static_cast<std::int64_t>(pairs.size());
            response["duplicates_found"] = static_cast<std::int64_t>(candidates.size());
            response["warning"] = !candidates.empty();
            response["candidates"] = candidate_array;
            response["threshold"] = threshold;
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
        } else if (apiPathMatches(path, "/api/qa/add")) {
            // POST /api/qa/add - add a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_id;
                std::string question;
                std::string answer;
                std::string category = "general";
                std::vector<std::string> tags;
                std::vector<std::string> aliases;
                std::map<std::string, std::string> metadata;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                    if (obj.contains("question")) {
                        question = obj.at("question").as_string().c_str();
                    }
                    if (obj.contains("answer")) {
                        answer = obj.at("answer").as_string().c_str();
                    }
                    if (obj.contains("category")) {
                        category = obj.at("category").as_string().c_str();
                    }
                    tags = jsonStringArray(obj, "tags");
                    aliases = jsonStringArray(obj, "aliases");
                    metadata = jsonStringMap(obj, "metadata");
                }

                if (question.empty() || answer.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "question and answer are required");
                    return;
                }

#if QORNIX_HAS_SQLITE
                if (sqlite_source_ &&
                    (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
                    std::string pair_id = makeQaPairId(sqlite_source_->getId());
                    if (!tags.empty()) {
                        metadata["tags"] = boost::json::serialize(stringArrayJson(tags));
                    }
                    boost::json::object metadata_json;
                    for (const auto& [key, value] : metadata) {
                        metadata_json[key] = value;
                    }
                    if (!sqlite_source_->addQAPair(pair_id, question, answer, category,
                                                   boost::json::serialize(stringArrayJson(aliases)),
                                                   boost::json::serialize(metadata_json))) {
                        buildErrorResponse(res, http::status::internal_server_error, "Failed to add QA pair to SQLite source");
                        return;
                    }

                    boost::json::object response;
                    response["success"] = true;
                    response["message"] = "QA pair added";
                    response["pair_id"] = pair_id;
                    response["source_id"] = sqlite_source_->getId();

                    buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
                    return;
                }
#endif

                // Find QASource by ID
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        if (source_id.empty() || qa->getId() == source_id) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found,
                                      "QASource not found: " + (source_id.empty() ? "no QA source" : source_id));
                    return;
                }

                // Generate ID if not provided
                std::string pair_id = makeQaPairId(source_id.empty() ? qa_source->getId() : source_id);

                QASource::QAPair new_pair;
                new_pair.id = pair_id;
                new_pair.question = question;
                new_pair.answer = answer;
                new_pair.category = category;
                new_pair.tags = tags;
                new_pair.aliases = aliases;
                new_pair.metadata = metadata;

                qa_source->addQAPair(new_pair);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair added";
                response["pair_id"] = pair_id;
                response["source_id"] = qa_source->getId();

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/qa/update")) {
            // POST /api/qa/update - update a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string pair_id;
                std::string question;
                std::string answer;
                std::string category;
                std::vector<std::string> tags;
                std::vector<std::string> aliases;
                std::map<std::string, std::string> metadata;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("pair_id")) {
                        pair_id = obj.at("pair_id").as_string().c_str();
                    }
                    if (obj.contains("question")) {
                        question = obj.at("question").as_string().c_str();
                    }
                    if (obj.contains("answer")) {
                        answer = obj.at("answer").as_string().c_str();
                    }
                    if (obj.contains("category")) {
                        category = obj.at("category").as_string().c_str();
                    }
                    tags = jsonStringArray(obj, "tags");
                    aliases = jsonStringArray(obj, "aliases");
                    metadata = jsonStringMap(obj, "metadata");
                }

                if (pair_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "pair_id is required");
                    return;
                }

#if QORNIX_HAS_SQLITE
                if (sqlite_source_) {
                    auto opt_pair = sqlite_source_->findQAPair(pair_id);
                    if (opt_pair) {
                        std::string updated_question = question.empty() ? opt_pair->question : question;
                        std::string updated_answer = answer.empty() ? opt_pair->answer : answer;
                        std::string updated_category = category.empty() ? opt_pair->category : category;
                        auto updated_metadata = opt_pair->metadata;
                        for (const auto& [key, value] : metadata) {
                            updated_metadata[key] = value;
                        }
                        if (!tags.empty()) {
                            updated_metadata["tags"] = boost::json::serialize(stringArrayJson(tags));
                        }
                        boost::json::object metadata_json;
                        for (const auto& [key, value] : updated_metadata) {
                            metadata_json[key] = value;
                        }
                        if (!sqlite_source_->updateQAPair(pair_id,
                                                          updated_answer,
                                                          updated_category,
                                                          aliases.empty() ? "" : boost::json::serialize(stringArrayJson(aliases)),
                                                          updated_question,
                                                          boost::json::serialize(metadata_json))) {
                            buildErrorResponse(res, http::status::internal_server_error, "Failed to update QA pair in SQLite source");
                            return;
                        }

                        boost::json::object response;
                        response["success"] = true;
                        response["message"] = "QA pair updated";
                        response["pair_id"] = pair_id;
                        response["source_id"] = sqlite_source_->getId();

                        buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
                        return;
                    }
                }
#endif

                // Find QASource containing this pair
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        auto opt_pair = qa->findQAPair(pair_id);
                        if (opt_pair) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                auto opt_pair = qa_source->findQAPair(pair_id);
                if (!opt_pair) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                QASource::QAPair updated_pair = opt_pair.value();
                if (!question.empty()) updated_pair.question = question;
                if (!answer.empty()) updated_pair.answer = answer;
                if (!category.empty()) updated_pair.category = category;
                if (!tags.empty()) updated_pair.tags = tags;
                if (!aliases.empty()) updated_pair.aliases = aliases;
                for (const auto& [key, value] : metadata) {
                    updated_pair.metadata[key] = value;
                }

                qa_source->updateQAPair(updated_pair);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair updated";
                response["pair_id"] = pair_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/qa/delete")) {
            // POST /api/qa/delete - delete a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string pair_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("pair_id")) {
                        pair_id = obj.at("pair_id").as_string().c_str();
                    }
                }

                if (pair_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "pair_id is required");
                    return;
                }

#if QORNIX_HAS_SQLITE
                if (sqlite_source_ && sqlite_source_->findQAPair(pair_id)) {
                    if (!sqlite_source_->deleteQAPair(pair_id)) {
                        buildErrorResponse(res, http::status::internal_server_error, "Failed to delete QA pair from SQLite source");
                        return;
                    }

                    boost::json::object response;
                    response["success"] = true;
                    response["message"] = "QA pair deleted";
                    response["pair_id"] = pair_id;
                    response["source_id"] = sqlite_source_->getId();

                    buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
                    return;
                }
#endif

                // Find QASource containing this pair
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        auto opt_pair = qa->findQAPair(pair_id);
                        if (opt_pair) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                qa_source->removeQAPair(pair_id);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair deleted";
                response["pair_id"] = pair_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/qa") || apiPathMatches(path, "/api/qa/list")) {
            // GET /api/qa/list - list QA pairs with pagination
            {
                std::string source_id;
                size_t page = 1;
                size_t per_page = 20;

                // Parse query parameters manually
                std::string query = url_view.query();
                if (!query.empty() && query[0] == '?') {
                    query = query.substr(1);
                }
                if (!query.empty()) {
                    // Simple query param parsing
                    std::stringstream ss(query);
                    std::string param;
                    while (std::getline(ss, param, '&')) {
                        auto eq_pos = param.find('=');
                        if (eq_pos != std::string::npos) {
                            std::string key = param.substr(0, eq_pos);
                            std::string value = param.substr(eq_pos + 1);
                            if (key == "source_id") {
                                source_id = value;
                            } else if (key == "page") {
                                page = static_cast<size_t>(std::stoi(value));
                            } else if (key == "per_page") {
                                per_page = static_cast<size_t>(std::stoi(value));
                            }
                        }
                    }
                }

                // Find QASource
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        if (source_id.empty() || qa->getId() == source_id) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "No QA source found");
                    return;
                }

                auto all_pairs = qa_source->getAllPairs();
                size_t total = all_pairs.size();
                size_t start = (page - 1) * per_page;
                size_t end = std::min(start + per_page, total);

                boost::json::array pairs_array;
                for (size_t i = start; i < end && i < total; i++) {
                    const auto& pair = all_pairs[i];
                    boost::json::object pair_obj;
                    pair_obj["id"] = pair.id;
                    pair_obj["question"] = pair.question;
                    pair_obj["category"] = pair.category;

                    boost::json::array aliases_array;
                    for (const auto& alias : pair.aliases) {
                        aliases_array.emplace_back(alias);
                    }
                    pair_obj["aliases"] = std::move(aliases_array);

                    pairs_array.emplace_back(pair_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["source_id"] = qa_source->getId();
                response["total"] = static_cast<std::int64_t>(total);
                response["page"] = static_cast<std::int64_t>(page);
                response["per_page"] = static_cast<std::int64_t>(per_page);
                response["pairs"] = pairs_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }

        // ============================================
        // Phase 5: New API Endpoints
        // ============================================

        } else if (apiPathMatches(path, "/api/analytics")) {
            // GET /api/analytics - Get analytics report
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto report = analytics_service_->getRecentReport();
                auto json_report = analytics_service_->exportToJson(report);

                boost::json::object response;
                response["success"] = true;
                response["report"] = boost::json::parse(json_report);

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/analytics/gaps")) {
            // GET /api/analytics/gaps - Get knowledge gaps
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto gaps = analytics_service_->getKnowledgeGaps();
                auto missing = analytics_service_->getMissingAnswers();

                boost::json::array gaps_array;
                for (const auto& gap : gaps) {
                    boost::json::object gap_obj;
                    gap_obj["query"] = gap.query;
                    gap_obj["search_count"] = static_cast<std::int64_t>(gap.search_count);
                    gaps_array.emplace_back(gap_obj);
                }

                boost::json::array missing_array;
                for (const auto& miss : missing) {
                    boost::json::object miss_obj;
                    miss_obj["query"] = miss.query;
                    miss_obj["search_count"] = static_cast<std::int64_t>(miss.search_count);
                    missing_array.emplace_back(miss_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["knowledge_gaps"] = gaps_array;
                response["missing_answers"] = missing_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/analytics/export")) {
            // POST /api/analytics/export - Export report to JSON
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto report = analytics_service_->getRecentReport();
                auto json_report = analytics_service_->exportToJson(report);

                res.set("Content-Type", "application/json; charset=utf-8");
                res.body() = json_report;
                res.result(http::status::ok);
            }
        } else if (apiPathMatches(path, "/api/feedback")) {
            if (!analytics_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                return;
            }
            boost::json::value json_req = boost::json::parse(req.body());
            if (!json_req.is_object()) {
                buildErrorResponse(res, http::status::bad_request, "Feedback JSON object is required");
                return;
            }
            const auto& obj = json_req.as_object();
            RagServiceFeedbackEntry service_feedback;
            if (obj.contains("request_id") && obj.at("request_id").is_string()) service_feedback.request_id = obj.at("request_id").as_string().c_str();
            if (obj.contains("query") && obj.at("query").is_string()) service_feedback.query = obj.at("query").as_string().c_str();
            if (obj.contains("question") && obj.at("question").is_string()) service_feedback.question = obj.at("question").as_string().c_str();
            if (obj.contains("answer") && obj.at("answer").is_string()) service_feedback.answer = obj.at("answer").as_string().c_str();
            if (obj.contains("rating") && obj.at("rating").is_string()) service_feedback.rating = obj.at("rating").as_string().c_str();
            if (obj.contains("category") && obj.at("category").is_string()) service_feedback.category = obj.at("category").as_string().c_str();
            if (obj.contains("comment") && obj.at("comment").is_string()) service_feedback.comment = obj.at("comment").as_string().c_str();
            if (obj.contains("citations") && obj.at("citations").is_array()) {
                for (const auto& value : obj.at("citations").as_array()) {
                    if (value.is_string()) service_feedback.citations.emplace_back(value.as_string().c_str());
                }
            }
            service_feedback.client_ip = headerValue(req, "X-Forwarded-For");
            if (service_feedback.client_ip.empty()) {
                service_feedback.client_ip = headerValue(req, "X-Real-IP");
            }
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            service_feedback.id = "fb_" + std::to_string(now_ms);

            if (rag_service_ && !rag_service_->recordFeedback(service_feedback)) {
                buildErrorResponse(res, http::status::bad_request, "Feedback must include rating, comment, category, query, or question");
                return;
            }

            AnalyticsService::FeedbackEntry feedback;
            feedback.id = service_feedback.id;
            feedback.request_id = service_feedback.request_id;
            feedback.query = service_feedback.query;
            feedback.question = service_feedback.question;
            feedback.answer = service_feedback.answer;
            feedback.rating = service_feedback.rating;
            feedback.category = service_feedback.category;
            feedback.comment = service_feedback.comment;
            feedback.citations = service_feedback.citations;
            feedback.client_ip = service_feedback.client_ip;
            feedback.timestamp = std::chrono::system_clock::now();
            analytics_service_->logFeedback(feedback);

            boost::json::object response;
            response["success"] = true;
            response["id"] = feedback.id;
            response["feedback"] = feedbackJson(feedback);
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
        } else if (apiPathMatches(path, "/api/qa/import")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }
            boost::json::value json_req = boost::json::parse(req.body());
            std::string payload = req.body();
            std::string default_category = "general";
            if (json_req.is_object()) {
                const auto& obj = json_req.as_object();
                if (obj.contains("default_category") && obj.at("default_category").is_string()) {
                    default_category = obj.at("default_category").as_string().c_str();
                }
                if (obj.contains("pairs")) {
                    payload = boost::json::serialize(json_req);
                } else if (obj.contains("json") && obj.at("json").is_string()) {
                    payload = obj.at("json").as_string().c_str();
                }
            }
            auto imported = rag_service_->importQaPairsJson(payload, default_category);
            boost::json::array ids;
            for (const auto& id : imported.imported_ids) {
                ids.emplace_back(id);
            }
            boost::json::array errors;
            for (const auto& error : imported.error_messages) {
                errors.emplace_back(error);
            }
            boost::json::object response;
            response["success"] = imported.success;
            response["imported"] = static_cast<std::int64_t>(imported.imported);
            response["duplicates"] = static_cast<std::int64_t>(imported.duplicates);
            response["errors"] = static_cast<std::int64_t>(imported.errors);
            response["imported_ids"] = ids;
            response["error_messages"] = errors;
            buildJsonResponse(res, imported.errors == 0 ? http::status::ok : http::status::bad_request,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/export")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }
            boost::json::value json_req = req.body().empty() ? boost::json::object{} : boost::json::parse(req.body());
            RagServiceQaListOptions options;
            options.limit = 500;
            if (json_req.is_object()) {
                const auto& obj = json_req.as_object();
                if (obj.contains("category") && obj.at("category").is_string()) {
                    options.category = obj.at("category").as_string().c_str();
                }
                if (obj.contains("tag") && obj.at("tag").is_string()) {
                    options.tag = obj.at("tag").as_string().c_str();
                }
                if (obj.contains("query") && obj.at("query").is_string()) {
                    options.query = obj.at("query").as_string().c_str();
                }
                if (obj.contains("limit") && obj.at("limit").is_int64()) {
                    options.limit = static_cast<size_t>(obj.at("limit").as_int64());
                }
                if (obj.contains("offset") && obj.at("offset").is_int64()) {
                    options.offset = static_cast<size_t>(obj.at("offset").as_int64());
                }
            }
            buildJsonResponse(res, http::status::ok, rag_service_->exportQaPairsJson(options));

        } else if (apiPathMatches(path, "/api/qa/dedup")) {
            // POST /api/qa/dedup - Find duplicate QA pairs (semantic deduplication)
            {
                if (!dedup_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "DeduplicationService not configured");
                    return;
                }

                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_id;
                float threshold = 0.85f;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                    if (obj.contains("threshold")) {
                        threshold = static_cast<float>(obj.at("threshold").as_double());
                    }
                }

                // Load QA pairs from SQLiteSource if source_id specified
                std::vector<QASource::QAPair> pairs;
#if QORNIX_HAS_SQLITE
                if (!source_id.empty() && sqlite_source_ && sqlite_source_->getSourceId() == source_id) {
                    pairs = sqlite_source_->getAllPairs();
                } else
#endif
                if (!source_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "Unknown source_id: " + source_id);
                    return;
                } else {
                    // If no source_id, use all registered data sources
                    auto sources = rag_engine_->getDataSources();
                    for (const auto& source : sources) {
                        if (source->getType() == DataSourceType::DATABASE) {
#if QORNIX_HAS_SQLITE
                            if (sqlite_source_ && sqlite_source_->count() > 0) {
                                auto sql_pairs = sqlite_source_->getAllPairs();
                                pairs.insert(pairs.end(), sql_pairs.begin(), sql_pairs.end());
                            }
#endif
                        } else if (source->getType() == DataSourceType::QA_KB) {
                            // QASource
                            auto qa_source = std::dynamic_pointer_cast<QASource>(source);
                            if (qa_source) {
                                auto qa_pairs = qa_source->getAllPairs();
                                pairs.insert(pairs.end(), qa_pairs.begin(), qa_pairs.end());
                            }
                        }
                    }
                }

                if (pairs.empty()) {
                    boost::json::object response;
                    response["success"] = true;
                    response["message"] = "No QA pairs found to check";
                    response["total_pairs"] = static_cast<std::int64_t>(0);
                    response["duplicates_found"] = static_cast<std::int64_t>(0);
                    response["duplicates"] = boost::json::array{};
                    buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
                    return;
                }

                // Run deduplication
                auto dedup_config = dedup_service_->getConfig();
                dedup_config.similarity_threshold = threshold;
                dedup_service_->setConfig(dedup_config);

                auto result = dedup_service_->findDuplicates(pairs, *rag_engine_);

                // Build response
                boost::json::array duplicates_array;
                for (const auto& dup : result.duplicates) {
                    boost::json::object dup_obj{
                        {"primary_id", dup.primary_id},
                        {"duplicate_id", dup.duplicate_id},
                        {"similarity", static_cast<double>(dup.similarity)},
                        {"reason", dup.reason}
                    };
                    duplicates_array.emplace_back(dup_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["result"] = {
                    {"total_pairs", static_cast<std::int64_t>(result.total_pairs)},
                    {"duplicates_found", static_cast<std::int64_t>(result.duplicates_found)},
                    {"duplicates", duplicates_array}
                };

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/qa/dedup/remove")) {
            // POST /api/qa/dedup/remove - Remove duplicates
            {
                if (!dedup_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "DeduplicationService not configured");
                    return;
                }

#if QORNIX_HAS_SQLITE
                if (!sqlite_source_) {
                    buildErrorResponse(res, http::status::service_unavailable, "SQLiteSource not configured");
                    return;
                }

                boost::json::value json_req = boost::json::parse(req.body());
                float threshold = 0.85f;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("threshold")) {
                        threshold = static_cast<float>(obj.at("threshold").as_double());
                    }
                }

                // Find duplicates first
                auto pairs = sqlite_source_->getAllPairs();
                auto dedup_config = dedup_service_->getConfig();
                dedup_config.similarity_threshold = threshold;
                dedup_service_->setConfig(dedup_config);

                auto result = dedup_service_->findDuplicates(pairs, *rag_engine_);

                // Remove duplicates
                size_t removed = dedup_service_->removeDuplicates(*sqlite_source_, result.duplicates);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "Duplicates removed";
                response["duplicates_found"] = static_cast<std::int64_t>(result.duplicates_found);
                response["duplicates_removed"] = static_cast<std::int64_t>(removed);

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
#else
                buildErrorResponse(res, http::status::service_unavailable, "SQLiteSource not available");
#endif
            }
        } else if (apiPathMatches(path, "/api/import/markdown")) {
            // POST /api/import/markdown - Import Markdown files
            {
                if (!markdown_source_) {
                    buildErrorResponse(res, http::status::service_unavailable, "MarkdownSource not configured");
                    return;
                }

                boost::json::value json_req = boost::json::parse(req.body());
                std::string directory;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("directory")) {
                        directory = obj.at("directory").as_string().c_str();
                    }
                }

                if (!directory.empty()) {
                    markdown_source_->setDirectoryPath(directory);
                }

                auto result = markdown_source_->importFiles();

                boost::json::object response;
                response["success"] = true;
                response["result"] = {
                    {"files_imported", static_cast<std::int64_t>(result.files_imported)},
                    {"documents_created", static_cast<std::int64_t>(result.documents_created)},
                    {"duplicates_skipped", static_cast<std::int64_t>(result.duplicates_skipped)}
                };

                boost::json::array errors_array;
                for (const auto& err : result.errors) {
                    errors_array.emplace_back(err);
                }
                response["errors"] = errors_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (apiPathMatches(path, "/api/import/history")) {
            // GET /api/import/history - Get import history
            {
                if (!markdown_source_) {
                    buildErrorResponse(res, http::status::service_unavailable, "MarkdownSource not configured");
                    return;
                }

                auto imported_files = markdown_source_->getImportedFiles();

                boost::json::array files_array;
                for (const auto& file : imported_files) {
                    files_array.emplace_back(file);
                }

                boost::json::object response;
                response["success"] = true;
                response["files"] = files_array;
                response["count"] = static_cast<std::int64_t>(imported_files.size());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else {
            buildErrorResponse(res, http::status::not_found, "Unknown endpoint");
        }
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

// ============================================================================
// RagApiHandler::handlePut / handleDelete
// ============================================================================

void RagApiHandler::handlePut(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &path_params) {
    try {
        if (!authorizeRequest(req, res, url_view, "PUT")) {
            return;
        }
        const std::string path = url_view.path();
        if (!path_params.count("id") || path.find("/qa/") == std::string::npos) {
            buildErrorResponse(res, http::status::not_found, "Endpoint not found");
            return;
        }

        boost::json::value json_req = boost::json::parse(req.body());
        std::string question;
        std::string answer;
        std::string category;
        std::vector<std::string> tags;
        std::vector<std::string> aliases;
        std::map<std::string, std::string> metadata;

        if (json_req.if_object()) {
            auto obj = json_req.as_object();
            if (obj.contains("question")) {
                question = obj.at("question").as_string().c_str();
            }
            if (obj.contains("answer")) {
                answer = obj.at("answer").as_string().c_str();
            }
            if (obj.contains("category")) {
                category = obj.at("category").as_string().c_str();
            }
            tags = jsonStringArray(obj, "tags");
            aliases = jsonStringArray(obj, "aliases");
            metadata = jsonStringMap(obj, "metadata");
        }

        const std::string pair_id = path_params.at("id");
        if (!rag_service_ || !rag_service_->updateQaPair(pair_id, question, answer, category, tags, aliases, metadata)) {
            buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
            return;
        }

        boost::json::object response;
        response["success"] = true;
        response["message"] = "QA pair updated";
        response["pair_id"] = pair_id;
        buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

void RagApiHandler::handleDelete(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &path_params) {
    try {
        if (!authorizeRequest(req, res, url_view, "DELETE")) {
            return;
        }
        const std::string path = url_view.path();
        if (!path_params.count("id") || path.find("/qa/") == std::string::npos) {
            buildErrorResponse(res, http::status::not_found, "Endpoint not found");
            return;
        }

        const std::string pair_id = path_params.at("id");
        if (!rag_service_ || !rag_service_->deleteQaPair(pair_id)) {
            buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
            return;
        }

        boost::json::object response;
        response["success"] = true;
        response["message"] = "QA pair deleted";
        response["pair_id"] = pair_id;
        buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

// ============================================================================
// RagApiHandler::handleGet
// ============================================================================

void RagApiHandler::handleGet(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &path_params) {
    try {
        if (!authorizeRequest(req, res, url_view, "GET")) {
            return;
        }
        std::string path = url_view.path();

        if (apiPathMatches(path, "/api/metrics")) {
            if (!metrics_) {
                buildErrorResponse(res, http::status::service_unavailable, "Metrics not enabled");
                return;
            }
            res.result(http::status::ok);
            res.set("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
            res.body() = metrics_->render_all();
            res.prepare_payload();

        } else if (apiPathMatches(path, "/api/admin/diagnostics")) {
            auto stats = rag_engine_ ? rag_engine_->get_statistics() : ProjectStats{};
            auto health = rag_service_ ? rag_service_->health() : RagServiceHealth{};

            boost::json::object rag_obj;
            rag_obj["indexed"] = rag_engine_ && rag_engine_->is_indexed();
            rag_obj["project_root"] = rag_engine_ ? rag_engine_->get_indexed_project_root() : "";
            rag_obj["files"] = static_cast<std::int64_t>(stats.total_files);
            rag_obj["lines"] = static_cast<std::int64_t>(stats.total_lines);
            rag_obj["indexed_chunks"] = static_cast<std::int64_t>(stats.indexed_chunks);
            rag_obj["reused_embeddings"] = static_cast<std::int64_t>(stats.reused_embeddings);
            rag_obj["generated_embeddings"] = static_cast<std::int64_t>(stats.generated_embeddings);
            rag_obj["stale_embeddings"] = static_cast<std::int64_t>(stats.stale_embeddings);
            rag_obj["index_duration_ms"] = static_cast<std::int64_t>(stats.index_duration_ms);
            rag_obj["embedding_backend"] = health.embedding_backend;
            rag_obj["embedding_model_id"] = health.embedding_model_id;
            rag_obj["embedding_dim"] = static_cast<std::int64_t>(health.embedding_dim);
            rag_obj["embedding_active_model_id"] = health.embedding_active_model_id;
            rag_obj["embedding_registry_size"] = static_cast<std::int64_t>(health.embedding_registry_size);
            boost::json::array registry_models;
            for (const auto& model_id : health.embedding_registry_model_ids) {
                registry_models.emplace_back(model_id);
            }
            rag_obj["embedding_registry_model_ids"] = std::move(registry_models);
            boost::json::array registry_warnings;
            for (const auto& warning : health.embedding_registry_warnings) {
                registry_warnings.emplace_back(warning);
            }
            rag_obj["embedding_registry_warnings"] = std::move(registry_warnings);
            rag_obj["vector_store_backend"] = health.vector_store_backend;
            rag_obj["vector_store_status"] = health.vector_store_status;
            rag_obj["vector_store_health_status"] = health.vector_store_health_status;
            rag_obj["vector_store_health_detail"] = health.vector_store_health_detail;
            rag_obj["vector_store_ready"] = health.vector_store_ready;
            rag_obj["vector_store_size"] = static_cast<std::int64_t>(health.vector_store_size);
            rag_obj["vector_store_dimension"] = static_cast<std::int64_t>(health.vector_store_dimension);
            rag_obj["query_expansion"] = health.query_expansion;
            rag_obj["multi_query_retrieval"] = health.multi_query_retrieval;
            rag_obj["embedding_reranker"] = health.embedding_reranker;
            rag_obj["reranking"] = health.reranking;
            boost::json::object xapian_obj;
            xapian_obj["enabled"] = health.xapian.enabled;
            xapian_obj["ready"] = health.xapian.ready;
            xapian_obj["status"] = health.xapian.status;
            xapian_obj["detail"] = health.xapian.detail;
            xapian_obj["language"] = health.xapian.language;
            xapian_obj["effective_index_language"] = health.xapian.effective_index_language;
            xapian_obj["effective_query_language"] = health.xapian.effective_query_language;
            xapian_obj["stemming"] = health.xapian.stemming;
            xapian_obj["stemming_strategy"] = health.xapian.stemming_strategy;
            xapian_obj["cjk_ngrams"] = health.xapian.cjk_ngrams;
            xapian_obj["word_breaks"] = health.xapian.word_breaks;
            xapian_obj["spelling"] = health.xapian.spelling;
            xapian_obj["metadata_prefixes"] = health.xapian.metadata_prefixes;
            xapian_obj["documents_indexed"] = static_cast<std::int64_t>(health.xapian.documents_indexed);
            rag_obj["xapian"] = std::move(xapian_obj);
            rag_obj["grounding_api"] = true;

            boost::json::object llm_obj;
            llm_obj["status"] = health.llm_status;
            llm_obj["provider"] = health.llm_provider;
            llm_obj["model"] = health.llm_model;
            llm_obj["available"] = health.llm_available;
            llm_obj["provider_available"] = health.llm_provider_available;
            llm_obj["ready"] = health.llm_ready;
            llm_obj["configured_model_available"] = health.configured_model_available;
            llm_obj["response_time_ms"] = static_cast<std::int64_t>(health.llm_response_time_ms);

            boost::json::object cache_obj;
            cache_obj["enabled"] = static_cast<bool>(cache_);
            if (cache_) {
                cache_obj["backend"] = cache_->backend_name();
                cache_obj["available"] = cache_->is_available();
                auto cache_stats = cache_->get_stats();
                cache_obj["hits"] = static_cast<std::int64_t>(cache_stats.hits);
                cache_obj["misses"] = static_cast<std::int64_t>(cache_stats.misses);
                cache_obj["size"] = static_cast<std::int64_t>(cache_stats.size);
                cache_obj["max_size"] = static_cast<std::int64_t>(cache_stats.max_size);
                cache_obj["hit_rate_percent"] = cache_stats.hit_rate();
            }

            boost::json::object prompt_cache_obj;
            prompt_cache_obj["enabled"] = static_cast<bool>(prompt_cache_);
            if (prompt_cache_) {
                auto prompt_stats = prompt_cache_->get_stats();
                prompt_cache_obj["hits"] = static_cast<std::int64_t>(prompt_stats.hits);
                prompt_cache_obj["misses"] = static_cast<std::int64_t>(prompt_stats.misses);
                prompt_cache_obj["size"] = static_cast<std::int64_t>(prompt_stats.size);
                prompt_cache_obj["max_size"] = static_cast<std::int64_t>(prompt_stats.max_size);
                prompt_cache_obj["hit_rate_percent"] = prompt_stats.hit_rate();
            }

            boost::json::object rate_obj;
            rate_obj["enabled"] = rate_limiter_ && rate_limiter_->is_available();
            if (rate_limiter_) {
                auto rate_stats = rate_limiter_->get_stats();
                rate_obj["allowed"] = static_cast<std::int64_t>(rate_stats.allowed);
                rate_obj["rejected"] = static_cast<std::int64_t>(rate_stats.rejected);
                rate_obj["whitelisted"] = static_cast<std::int64_t>(rate_stats.whitelisted);
                rate_obj["rejection_rate_percent"] = rate_stats.rejection_rate();
            }

            boost::json::object storage_obj;
#if QORNIX_HAS_SQLITE
            storage_obj["sqlite_enabled"] = static_cast<bool>(sqlite_source_);
            if (sqlite_source_) {
                storage_obj["qa_pairs"] = static_cast<std::int64_t>(sqlite_source_->count());
                storage_obj["persisted_documents"] = static_cast<std::int64_t>(sqlite_source_->countPersistedDocuments());
                storage_obj["persisted_chunks"] = static_cast<std::int64_t>(sqlite_source_->countPersistedChunks());
                storage_obj["persisted_embeddings"] = static_cast<std::int64_t>(sqlite_source_->countPersistedEmbeddings());
            }
#else
            storage_obj["sqlite_enabled"] = false;
#endif

            boost::json::object upload_obj;
            upload_obj["enabled"] = upload_options_.enabled;
            upload_obj["uploads_dir"] = upload_options_.uploads_dir;
            upload_obj["max_file_size_kb"] = static_cast<std::int64_t>(upload_options_.max_file_size_kb);
            upload_obj["max_files_per_request"] = static_cast<std::int64_t>(upload_options_.max_files_per_request);
            upload_obj["auto_ingest"] = upload_options_.auto_ingest;
            upload_obj["async_ingest"] = upload_options_.async_ingest;
            upload_obj["allowed_extensions"] = stringArrayJson(upload_options_.allowed_extensions);
            upload_obj["allowed_mime_types"] = stringArrayJson(upload_options_.allowed_mime_types);

            boost::json::object response;
            response["success"] = true;
            response["status"] = health.status;
            response["rag"] = rag_obj;
            response["llm"] = llm_obj;
            response["cache"] = cache_obj;
            response["prompt_cache"] = prompt_cache_obj;
            response["rate_limit"] = rate_obj;
            response["storage"] = storage_obj;
            response["metrics_enabled"] = static_cast<bool>(metrics_);
            boost::json::object auth_obj;
            auth_obj["enabled"] = auth_options_.enabled;
            auth_obj["mode"] = auth_options_.mode;
            auth_obj["protect_admin_routes"] = auth_options_.protect_admin_routes;
            auth_obj["protect_write_routes"] = auth_options_.protect_write_routes;
            auth_obj["token_header"] = auth_options_.token_header;
            auth_obj["role_header"] = auth_options_.role_header;
            auth_obj["admin_role"] = auth_options_.admin_role;
            response["auth"] = auth_obj;
            response["upload"] = upload_obj;
            response["auth_required_by_rag"] = auth_options_.enabled;
            response["network_exposure_note"] = auth_options_.enabled
                ? "RAG admin/write routes are protected by the configured route auth baseline. Keep host app/proxy auth in front of exposed deployments."
                : "Protect this endpoint with the host app auth/proxy layer before exposing it outside a trusted network.";
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/embedding/models")) {
            if (!rag_service_) {
                buildErrorResponse(res, http::status::service_unavailable, "RAG service is not configured");
                return;
            }
            auto models = rag_service_->embeddingModels();
            boost::json::array model_array;
            for (const auto& model : models.models) {
                model_array.emplace_back(embeddingModelObject(model));
            }
            boost::json::array warnings_array;
            for (const auto& warning : models.warnings) {
                warnings_array.emplace_back(warning);
            }
            boost::json::object response;
            response["success"] = models.success;
            response["active_model_id"] = models.active_model_id;
            response["effective_model_id"] = models.effective_model_id;
            response["backend"] = models.backend;
            response["models"] = model_array;
            response["warnings"] = warnings_array;
            response["switch_endpoint"] = apiPathMatches(path, "/api/rag/embedding/models")
                ? "/api/rag/embedding/switch"
                : "/api/embedding/switch";
            buildJsonResponse(res, models.success ? http::status::ok : http::status::service_unavailable,
                              boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/ingest/jobs")) {
            size_t limit = 20;
            std::string query = url_view.query();
            if (!query.empty() && query[0] == '?') {
                query = query.substr(1);
            }
            if (!query.empty()) {
                std::stringstream ss(query);
                std::string param;
                while (std::getline(ss, param, '&')) {
                    auto eq_pos = param.find('=');
                    if (eq_pos != std::string::npos && param.substr(0, eq_pos) == "limit") {
                        limit = static_cast<size_t>(std::stoul(param.substr(eq_pos + 1)));
                    }
                }
            }

            auto jobs = rag_service_->listIngestionJobs(limit);
            boost::json::array jobs_array;
            for (const auto& job : jobs) {
                jobs_array.emplace_back(ingestionJobObject(job));
            }

            boost::json::object response;
            response["success"] = true;
            response["jobs"] = jobs_array;
            response["count"] = static_cast<std::int64_t>(jobs.size());
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (path_params.count("id") && path.find("/ingest/") != std::string::npos) {
            const std::string job_id = path_params.at("id");
            auto job = rag_service_->findIngestionJob(job_id);
            if (!job) {
                buildErrorResponse(res, http::status::not_found, "Ingestion job not found: " + job_id);
                return;
            }

            boost::json::object response;
            response["success"] = true;
            response["job"] = ingestionJobObject(*job);
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/sources")) {
            auto sources = rag_engine_->getDataSources();
            boost::json::array sources_array;

            for (const auto& source : sources) {
                boost::json::object source_obj{
                    {"id", source->getId()},
                    {"type", data_source_type_to_string(source->getType())},
                    {"name", source->getName()},
                    {"document_count", static_cast<std::int64_t>(source->count())}
                };
                sources_array.emplace_back(source_obj);
            }

            boost::json::object response;
            response["success"] = true;
            response["sources"] = sources_array;
            response["count"] = static_cast<std::int64_t>(sources.size());

            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/suggest")) {
            const auto params = parseQueryParams(url_view);
            const std::string query = params.count("q") ? params.at("q") : "";
            const std::string source_id = params.count("source_id") ? params.at("source_id") : "";
            const size_t limit = querySize(params, "limit", 10, 50);

            boost::json::array suggestions_array;
            if (rag_service_) {
                for (const auto& item : rag_service_->suggestQaPairs(query, limit, source_id)) {
                    boost::json::object obj;
                    obj["id"] = item.id;
                    obj["question"] = item.question;
                    obj["category"] = item.category;
                    suggestions_array.emplace_back(std::move(obj));
                }
            }

            boost::json::object response;
            response["success"] = true;
            response["query"] = query;
            response["suggestions"] = suggestions_array;
            response["count"] = static_cast<std::int64_t>(suggestions_array.size());
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/categories")) {
            const auto params = parseQueryParams(url_view);
            const std::string query = params.count("q") ? params.at("q") : "";
            const std::string source_id = params.count("source_id") ? params.at("source_id") : "";
            const size_t limit = querySize(params, "limit", 50, 200);

            boost::json::array categories_array;
            if (rag_service_) {
                for (const auto& category : rag_service_->listQaCategories(query, limit, source_id)) {
                    categories_array.emplace_back(category);
                }
            }

            boost::json::object response;
            response["success"] = true;
            response["query"] = query;
            response["categories"] = categories_array;
            response["count"] = static_cast<std::int64_t>(categories_array.size());
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/tags")) {
            const auto params = parseQueryParams(url_view);
            const std::string query = params.count("q") ? params.at("q") : "";
            const std::string source_id = params.count("source_id") ? params.at("source_id") : "";
            const size_t limit = querySize(params, "limit", 50, 200);

            boost::json::array tags_array;
            if (rag_service_) {
                for (const auto& tag : rag_service_->listQaTags(query, limit, source_id)) {
                    tags_array.emplace_back(tag);
                }
            }

            boost::json::object response;
            response["success"] = true;
            response["query"] = query;
            response["tags"] = tags_array;
            response["count"] = static_cast<std::int64_t>(tags_array.size());
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/export")) {
            const auto params = parseQueryParams(url_view);
            RagServiceQaListOptions options;
            options.query = params.count("query") ? params.at("query") : (params.count("q") ? params.at("q") : "");
            options.category = params.count("category") ? params.at("category") : "";
            options.tag = params.count("tag") ? params.at("tag") : "";
            options.source_id = params.count("source_id") ? params.at("source_id") : "";
            options.limit = querySize(params, "limit", 500, 5000);
            options.offset = querySize(params, "offset", 0, std::numeric_limits<size_t>::max());
            buildJsonResponse(res, http::status::ok,
                              rag_service_ ? rag_service_->exportQaPairsJson(options) : "{}");

        } else if (apiPathMatches(path, "/api/qa") || apiPathMatches(path, "/api/qa/list")) {
            const auto params = parseQueryParams(url_view);
            const size_t limit = querySize(params, "limit", querySize(params, "per_page", 25, 500), 500);
            size_t offset = querySize(params, "offset", 0, std::numeric_limits<size_t>::max());
            const size_t page = querySize(params, "page", 0, std::numeric_limits<size_t>::max());
            if (page > 0) {
                offset = (page - 1) * limit;
            }

            RagServiceQaListOptions options;
            options.query = params.count("query") ? params.at("query") : (params.count("q") ? params.at("q") : "");
            options.category = params.count("category") ? params.at("category") : "";
            options.tag = params.count("tag") ? params.at("tag") : "";
            options.source_id = params.count("source_id") ? params.at("source_id") : "";
            options.limit = limit;
            options.offset = offset;

            auto listed = rag_service_ ? rag_service_->listQaPairs(options) : RagServiceQaListResponse{};

            boost::json::array pairs_array;
            for (const auto& pair : listed.items) {
                pairs_array.emplace_back(qaPairObject(pair));
            }

            boost::json::object response;
            response["success"] = true;
            response["source_id"] = listed.source_id;
            response["items"] = pairs_array;
            response["pairs"] = pairs_array;
            response["total"] = static_cast<std::int64_t>(listed.total);
            response["limit"] = static_cast<std::int64_t>(listed.limit);
            response["offset"] = static_cast<std::int64_t>(listed.offset);
            response["has_more"] = listed.has_more;
            response["page"] = static_cast<std::int64_t>(listed.limit == 0 ? 1 : (listed.offset / listed.limit) + 1);
            response["per_page"] = static_cast<std::int64_t>(listed.limit);
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));

        } else if (apiPathMatches(path, "/api/qa/history")) {
#if QORNIX_HAS_SQLITE
            if (!sqlite_source_) {
                buildErrorResponse(res, http::status::service_unavailable, "SQLite QA source is not configured");
                return;
            }
            const auto params = parseQueryParams(url_view);
            const std::string pair_id = params.count("pair_id") ? params.at("pair_id") : (params.count("id") ? params.at("id") : "");
            const size_t limit = querySize(params, "limit", 25, 200);
            if (pair_id.empty()) {
                buildErrorResponse(res, http::status::bad_request, "pair_id is required");
                return;
            }
            const auto history = sqlite_source_->getQAPairHistory(pair_id, limit);
            boost::json::array items;
            for (const auto& entry : history) {
                boost::json::object item;
                item["pair_id"] = entry.pair_id;
                item["source_id"] = entry.source_id;
                item["version"] = entry.version;
                item["action"] = entry.action;
                item["question"] = entry.question;
                item["answer"] = entry.answer;
                item["category"] = entry.category;
                item["aliases"] = entry.aliases;
                try {
                    item["metadata"] = boost::json::parse(entry.metadata);
                } catch (...) {
                    item["metadata"] = entry.metadata;
                }
                item["changed_at"] = entry.changed_at;
                items.emplace_back(std::move(item));
            }
            boost::json::object response;
            response["success"] = true;
            response["pair_id"] = pair_id;
            response["items"] = items;
            response["total"] = static_cast<std::int64_t>(history.size());
            buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
#else
            buildErrorResponse(res, http::status::service_unavailable, "SQLite support is not available");
#endif

        } else if (apiPathMatches(path, "/api/health")) {
            // Health check endpoint
            auto stats = rag_engine_->get_statistics();

            boost::json::object rag_obj;
            rag_obj["indexed"] = rag_engine_->is_indexed();
            rag_obj["files"] = stats.total_files;
            rag_obj["lines"] = stats.total_lines;
            rag_obj["indexed_chunks"] = stats.indexed_chunks;
            rag_obj["reused_embeddings"] = stats.reused_embeddings;
            rag_obj["generated_embeddings"] = stats.generated_embeddings;
            rag_obj["stale_embeddings"] = stats.stale_embeddings;
            rag_obj["embedding_backend"] = rag_engine_->get_embedding_backend();
            rag_obj["embedding_model_id"] = rag_engine_->get_embedding_model_id();
            rag_obj["embedding_dim"] = static_cast<std::int64_t>(rag_engine_->get_embedding_dim());
            const auto embedding_info = rag_engine_->get_embedding_model_info();
            rag_obj["embedding_active_model_id"] = embedding_info.active_model_id;
            rag_obj["embedding_registry_size"] = static_cast<std::int64_t>(embedding_info.registry_size);
            boost::json::array registry_models;
            for (const auto& model_id : embedding_info.registry_model_ids) {
                registry_models.emplace_back(model_id);
            }
            rag_obj["embedding_registry_model_ids"] = std::move(registry_models);
            boost::json::array registry_warnings;
            for (const auto& warning : embedding_info.registry_warnings) {
                registry_warnings.emplace_back(warning);
            }
            rag_obj["embedding_registry_warnings"] = std::move(registry_warnings);
            rag_obj["vector_store_backend"] = rag_engine_->get_vector_store_backend();
            rag_obj["vector_store_status"] = rag_engine_->get_vector_store_status();
            const auto vector_diagnostics = rag_engine_->get_vector_store_diagnostics();
            rag_obj["vector_store_health_status"] = vector_diagnostics.status;
            rag_obj["vector_store_health_detail"] = vector_diagnostics.detail;
            rag_obj["vector_store_ready"] = vector_diagnostics.ready;
            rag_obj["vector_store_size"] = static_cast<std::int64_t>(vector_diagnostics.size);
            rag_obj["vector_store_dimension"] = static_cast<std::int64_t>(vector_diagnostics.dimension);
            rag_obj["hybrid_search"] = true; // Default to true
            rag_obj["query_expansion"] = rag_engine_->is_query_expansion_enabled();
            rag_obj["multi_query_retrieval"] = rag_engine_->is_multi_query_retrieval_enabled();
            rag_obj["embedding_reranker"] = rag_engine_->is_embedding_reranker_enabled();
            rag_obj["reranking"] = rag_engine_->is_reranking_enabled();
            const auto xapian_diagnostics = rag_engine_->get_xapian_diagnostics();
            boost::json::object xapian_obj;
            xapian_obj["enabled"] = xapian_diagnostics.enabled;
            xapian_obj["ready"] = xapian_diagnostics.ready;
            xapian_obj["status"] = xapian_diagnostics.status;
            xapian_obj["detail"] = xapian_diagnostics.detail;
            xapian_obj["language"] = xapian_diagnostics.language;
            xapian_obj["effective_index_language"] = xapian_diagnostics.effective_index_language;
            xapian_obj["effective_query_language"] = xapian_diagnostics.effective_query_language;
            xapian_obj["stemming"] = xapian_diagnostics.stemming;
            xapian_obj["stemming_strategy"] = xapian_diagnostics.stemming_strategy;
            xapian_obj["cjk_ngrams"] = xapian_diagnostics.cjk_ngrams;
            xapian_obj["word_breaks"] = xapian_diagnostics.word_breaks;
            xapian_obj["spelling"] = xapian_diagnostics.spelling;
            xapian_obj["metadata_prefixes"] = xapian_diagnostics.metadata_prefixes;
            xapian_obj["documents_indexed"] = static_cast<std::int64_t>(xapian_diagnostics.documents_indexed);
            rag_obj["xapian"] = std::move(xapian_obj);

            boost::json::object llm_obj;
            if (llm_client_ && llm_client_->is_enabled()) {
                const int response_time = llm_client_->provider_health_check();
                const auto models = llm_client_->list_available_models();
                const bool configured_model_available =
                    llm_client_->configured_model_available();
                const std::string provider = llm_client_->provider_name();
                const bool provider_available = response_time >= 0;
                const bool llm_ready = provider_available &&
                    ((provider != "ollama" && models.empty()) || configured_model_available);

                boost::json::array models_array;
                for (const auto& model : models) {
                    models_array.emplace_back(model);
                }

                llm_obj["available"] = provider_available;
                llm_obj["provider_available"] = provider_available;
                llm_obj["ready"] = llm_ready;
                llm_obj["provider"] = provider;
                llm_obj["model"] = llm_client_->get_model();
                llm_obj["configured_model_available"] = configured_model_available;
                llm_obj["available_models"] = std::move(models_array);
                llm_obj["api_url"] = llm_client_->get_api_url();
                if (!provider_available) {
                    llm_obj["status"] = "provider_unavailable";
                } else if (models.empty()) {
                    llm_obj["status"] = "models_unreported";
                } else if (!configured_model_available) {
                    llm_obj["status"] = "model_not_found";
                } else {
                    llm_obj["status"] = "ok";
                }
                llm_obj["response_time_ms"] = response_time >= 0
                                              ? static_cast<std::int64_t>(response_time)
                                              : static_cast<std::int64_t>(-1);
            } else {
                llm_obj["available"] = false;
                llm_obj["provider_available"] = false;
                llm_obj["ready"] = false;
                llm_obj["provider"] = "not_configured";
                llm_obj["model"] = "not_configured";
                llm_obj["configured_model_available"] = false;
                llm_obj["available_models"] = boost::json::array{};
                llm_obj["api_url"] = "N/A";
                llm_obj["status"] = "not_configured";
            }

            boost::json::object response;
            response["status"] = "ok";
            response["rag"] = rag_obj;
            response["llm"] = llm_obj;

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else {
            // Original /api/stats handling
            auto stats = rag_engine_->get_statistics();

            boost::json::object response;
            response["success"] = true;
            response["indexed"] = rag_engine_->is_indexed();
            response["embedding_backend"] = rag_engine_->get_embedding_backend();
            response["embedding_model_id"] = rag_engine_->get_embedding_model_id();
            response["embedding_dim"] = static_cast<std::int64_t>(rag_engine_->get_embedding_dim());
            response["vector_store_backend"] = rag_engine_->get_vector_store_backend();
            response["vector_store_status"] = rag_engine_->get_vector_store_status();
            const auto vector_diagnostics = rag_engine_->get_vector_store_diagnostics();
            response["vector_store_health_status"] = vector_diagnostics.status;
            response["vector_store_health_detail"] = vector_diagnostics.detail;
            response["vector_store_ready"] = vector_diagnostics.ready;
            response["vector_store_size"] = static_cast<std::int64_t>(vector_diagnostics.size);
            response["vector_store_dimension"] = static_cast<std::int64_t>(vector_diagnostics.dimension);
            response["query_expansion"] = rag_engine_->is_query_expansion_enabled();
            response["multi_query_retrieval"] = rag_engine_->is_multi_query_retrieval_enabled();
            response["embedding_reranker"] = rag_engine_->is_embedding_reranker_enabled();
            response["reranking"] = rag_engine_->is_reranking_enabled();
            const auto xapian_diagnostics = rag_engine_->get_xapian_diagnostics();
            boost::json::object xapian_obj;
            xapian_obj["enabled"] = xapian_diagnostics.enabled;
            xapian_obj["ready"] = xapian_diagnostics.ready;
            xapian_obj["status"] = xapian_diagnostics.status;
            xapian_obj["detail"] = xapian_diagnostics.detail;
            xapian_obj["language"] = xapian_diagnostics.language;
            xapian_obj["effective_index_language"] = xapian_diagnostics.effective_index_language;
            xapian_obj["effective_query_language"] = xapian_diagnostics.effective_query_language;
            xapian_obj["stemming"] = xapian_diagnostics.stemming;
            xapian_obj["stemming_strategy"] = xapian_diagnostics.stemming_strategy;
            xapian_obj["cjk_ngrams"] = xapian_diagnostics.cjk_ngrams;
            xapian_obj["word_breaks"] = xapian_diagnostics.word_breaks;
            xapian_obj["spelling"] = xapian_diagnostics.spelling;
            xapian_obj["metadata_prefixes"] = xapian_diagnostics.metadata_prefixes;
            xapian_obj["documents_indexed"] = static_cast<std::int64_t>(xapian_diagnostics.documents_indexed);
            response["xapian"] = std::move(xapian_obj);
            response["onnx_ready"] = rag_engine_->is_onnx_ready();
            response["onnx_status"] = rag_engine_->get_onnx_status_message();
            response["project_root"] = rag_engine_->get_indexed_project_root();

            boost::json::object stats_obj;
            stats_obj["total_files"] = stats.total_files;
            stats_obj["total_lines"] = stats.total_lines;
            stats_obj["total_size_kb"] = stats.total_size_bytes / 1024;
            stats_obj["indexed_chunks"] = stats.indexed_chunks;
            stats_obj["reused_embeddings"] = stats.reused_embeddings;
            stats_obj["generated_embeddings"] = stats.generated_embeddings;
            stats_obj["stale_embeddings"] = stats.stale_embeddings;
            stats_obj["index_duration_ms"] = static_cast<std::int64_t>(stats.index_duration_ms);

            boost::json::object files_by_type;
            for (const auto &[type, count]: stats.files_by_type) {
                files_by_type[type] = count;
            }
            stats_obj["files_by_type"] = files_by_type;

            boost::json::object files_by_dir;
            for (const auto &[dir, count]: stats.files_by_directory) {
                files_by_dir[dir] = count;
            }
            stats_obj["files_by_directory"] = files_by_dir;

            response["stats"] = stats_obj;

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        }
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

// ============================================================================
// RagWebHandler::handleGet
// ============================================================================

void RagWebHandler::handleGet(
    const http::request<http::string_body> &,
    http::response<http::string_body> &res,
    const urls::url_view &,
    const std::map<std::string, std::string> &) {
    try {
        std::string html = TemplateLoader::loadFile("rag_interface.html", templates_dir_);
        const std::string placeholder = "{{QORNIX_RAG_API_BASE}}";
        size_t pos = 0;
        while ((pos = html.find(placeholder, pos)) != std::string::npos) {
            html.replace(pos, placeholder.size(), api_base_);
            pos += api_base_.size();
        }
        buildHtmlResponse(res, http::status::ok, html);
    } catch (const std::exception &e) {
        std::string error_html = TemplateLoader::load500Template(templates_dir_);
        buildHtmlResponse(res, http::status::internal_server_error, error_html);
    }
}

// ============================================================================
// setupRagRoutes
// ============================================================================

void setupRagRoutes(HttpServer& server,
                    std::shared_ptr<RagEngine> rag_engine,
                    std::shared_ptr<LLMClient> llm_client,
                    std::shared_ptr<ICache> cache,
                    std::shared_ptr<RateLimiter> limiter,
                    std::shared_ptr<BatchProcessor> batch,
                    std::shared_ptr<IPromptCache> pcache,
                    std::shared_ptr<LLMRAGMetrics> metrics,
                    std::shared_ptr<AnalyticsService> analytics,
                    std::shared_ptr<MarkdownSource> markdown,
                    std::shared_ptr<DeduplicationService> dedup
#if QORNIX_HAS_SQLITE
                    , std::shared_ptr<SQLiteSource> sqlite
#endif
                    , RagRouteOptions options
                    ) {
    auto rag_service = std::make_shared<RagService>(
        rag_engine,
        llm_client
#if QORNIX_HAS_SQLITE
        , sqlite
#endif
    );

    setupRagRoutes(
        server,
        rag_service,
        cache,
        limiter,
        batch,
        pcache,
        metrics,
        analytics,
        markdown,
        dedup
#if QORNIX_HAS_SQLITE
        , sqlite
#endif
        , std::move(options)
    );
}

void setupRagRoutes(HttpServer& server,
                    std::shared_ptr<RagService> rag_service,
                    std::shared_ptr<ICache> cache,
                    std::shared_ptr<RateLimiter> limiter,
                    std::shared_ptr<BatchProcessor> batch,
                    std::shared_ptr<IPromptCache> pcache,
                    std::shared_ptr<LLMRAGMetrics> metrics,
                    std::shared_ptr<AnalyticsService> analytics,
                    std::shared_ptr<MarkdownSource> markdown,
                    std::shared_ptr<DeduplicationService> dedup
#if QORNIX_HAS_SQLITE
                    , std::shared_ptr<SQLiteSource> sqlite
#endif
                    , RagRouteOptions options
                    ) {
    std::cout << "🔧📝 Adding RAG routes..." << std::endl;
    auto rag_engine = rag_service ? rag_service->engine() : nullptr;
    auto llm_client = rag_service ? rag_service->llm() : nullptr;

    options.api_prefix = normalizeRoutePrefix(options.api_prefix, "/api");
    options.ui_path = normalizeRoutePrefix(options.ui_path, "/");

    // Main page
    const std::string templates_dir = resolveRagTemplatesDir();
    if (options.expose_root_ui || options.ui_path != "/") {
        const std::string api_base = options.api_prefix == "/api" ? "" : options.api_prefix;
        server.add_route(options.ui_path, std::make_shared<RagWebHandler>(templates_dir, api_base));
        std::cout << "  \u2713 GET  " << options.ui_path << " - Web interface"
                  << " (templates: " << templates_dir << ")" << std::endl;
        if (options.expose_root_ui && options.ui_path != "/") {
            server.add_route("/", std::make_shared<RagWebHandler>(templates_dir, api_base));
            std::cout << "  * GET  / - Web interface"
                      << " (alias for " << options.ui_path << ")" << std::endl;
        }
    }

    // API endpoints. Keep one full handler for routes that need optional
    // runtime services such as SQLite-backed QA wiki. Without the same handler,
    // /api/qa/add can store pairs successfully while /api/ask and /api/search
    // cannot see them.
#if QORNIX_HAS_SQLITE
    auto full_handler = std::make_shared<RagApiHandler>(
        rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics,
        markdown, dedup, sqlite);
#else
    auto full_handler = std::make_shared<RagApiHandler>(
        rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics,
        markdown, dedup);
#endif
    full_handler->setAuthOptions(options.auth);
    full_handler->setUploadOptions(options.upload);

    const auto api_route = [&](const std::string& suffix) {
        return joinRoute(options.api_prefix, suffix);
    };

    server.add_route(api_route("/index"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/index") << " - Index scan_path" << std::endl;

    server.add_route(api_route("/ingest"), full_handler);
    std::cout << "  ✓ POST " << api_route("/ingest") << " - Ingestion job" << std::endl;

    server.add_route(api_route("/documents/upload"), full_handler);
    std::cout << "  ✓ POST " << api_route("/documents/upload") << " - Secure document upload" << std::endl;

    server.add_route(api_route("/uploads/delete"), full_handler);
    std::cout << "  ✓ POST " << api_route("/uploads/delete") << " - Delete uploaded document" << std::endl;

    server.add_route(api_route("/ingest/jobs"), full_handler);
    std::cout << "  ✓ GET  " << api_route("/ingest/jobs") << " - Ingestion job history" << std::endl;

    server.add_route(api_route("/ingest/{id}"), full_handler);
    std::cout << "  ✓ GET  " << api_route("/ingest/{id}") << " - Ingestion job status" << std::endl;

    server.add_route(api_route("/documents/delete"), full_handler);
    std::cout << "  ✓ POST " << api_route("/documents/delete") << " - Delete persisted document" << std::endl;

    server.add_route(api_route("/search"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/search") << " - Search" << std::endl;

    server.add_route(api_route("/stats"), full_handler);
    std::cout << "  \u2713 GET  " << api_route("/stats") << " - Statistics" << std::endl;

    server.add_route(api_route("/ask"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/ask") << " - Ask with LLM" << std::endl;

    server.add_route(api_route("/batch"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/batch") << " - Batch questions" << std::endl;

    server.add_route(api_route("/health"), full_handler);
    std::cout << "  \u2713 GET  " << api_route("/health") << " - Health check" << std::endl;

    server.add_route(api_route("/metrics"), full_handler);
    std::cout << "  \u2713 GET  " << api_route("/metrics") << " - Prometheus metrics" << std::endl;

    server.add_route(api_route("/admin/diagnostics"), full_handler);
    std::cout << "  ✓ GET  " << api_route("/admin/diagnostics") << " - Admin diagnostics" << std::endl;

    server.add_route(api_route("/embedding/models"), full_handler);
    std::cout << "  ✓ GET  " << api_route("/embedding/models") << " - Embedding model registry" << std::endl;

    server.add_route(api_route("/embedding/switch"), full_handler);
    std::cout << "  ✓ POST " << api_route("/embedding/switch") << " - Switch embedding model" << std::endl;

    // Data source management endpoints
    server.add_route(api_route("/sources"), full_handler);
    std::cout << "  \u2713 GET  " << api_route("/sources") << " - Data source list" << std::endl;

    server.add_route(api_route("/sources/add"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/sources/add") << " - Add data source" << std::endl;

    server.add_route(api_route("/sources/remove"), full_handler);
    std::cout << "  \u2713 POST " << api_route("/sources/remove") << " - Remove data source" << std::endl;

    // QA knowledge base endpoints
    auto qa_handler = full_handler;

    server.add_route(api_route("/qa/add"), qa_handler);
    std::cout << "  ✓ POST " << api_route("/qa/add") << " - Add QA pair" << std::endl;

    server.add_route(api_route("/qa/duplicate-check"), qa_handler);
    std::cout << "  ✓ POST " << api_route("/qa/duplicate-check") << " - QA duplicate preview" << std::endl;

    server.add_route(api_route("/qa/update"), qa_handler);
    std::cout << "  ✓ POST " << api_route("/qa/update") << " - Update QA pair" << std::endl;

    server.add_route(api_route("/qa/delete"), qa_handler);
    std::cout << "  ✓ POST " << api_route("/qa/delete") << " - Delete QA pair" << std::endl;

    server.add_route(api_route("/qa/list"), qa_handler);
    std::cout << "  ✓ GET  " << api_route("/qa/list") << " - QA pair list" << std::endl;

    server.add_route(api_route("/qa/suggest"), qa_handler);
    std::cout << "  ✓ GET  " << api_route("/qa/suggest") << " - QA pair suggestions" << std::endl;

    server.add_route(api_route("/qa/categories"), qa_handler);
    std::cout << "  ✓ GET  " << api_route("/qa/categories") << " - QA pair categories" << std::endl;

    server.add_route(api_route("/qa/tags"), qa_handler);
    std::cout << "  ✓ GET  " << api_route("/qa/tags") << " - QA tags" << std::endl;

    server.add_route(api_route("/qa/history"), qa_handler);
    std::cout << "  ✓ GET  " << api_route("/qa/history") << " - QA version history" << std::endl;

    server.add_route(api_route("/qa/import"), qa_handler);
    std::cout << "  ✓ POST " << api_route("/qa/import") << " - Import QA pairs" << std::endl;

    server.add_route(api_route("/qa/export"), qa_handler);
    std::cout << "  ✓ GET/POST " << api_route("/qa/export") << " - Export QA pairs" << std::endl;

    server.add_route(api_route("/qa"), qa_handler);
    std::cout << "  ✓ GET/POST " << api_route("/qa") << " - QA REST collection" << std::endl;

    server.add_route(api_route("/qa/{id}"), qa_handler);
    std::cout << "  ✓ PUT/DELETE " << api_route("/qa/{id}") << " - QA REST item" << std::endl;

    if (llm_client && llm_client->is_enabled()) {
        std::cout << "  \u2139\ufe0f  LLM: " << llm_client->get_model()
                  << " @ " << llm_client->get_api_url() << std::endl;
    } else {
        std::cout << "  \u2139\ufe0f  LLM: not configured (search-only mode)" << std::endl;
    }

    if (cache) {
        std::cout << "  \u2705 Cache: enabled" << std::endl;
    }
    if (limiter && limiter->is_available()) {
        std::cout << "  \U0001f6e1\ufe0f  Rate limiter: enabled" << std::endl;
    }
    if (batch) {
        std::cout << "  \U0001f4e6 Batch processor: enabled" << std::endl;
    }
    if (pcache) {
        std::cout << "  \U0001f4dd Prompt cache: enabled" << std::endl;
    }
    if (metrics) {
        std::cout << "  \U0001f4ca Prometheus metrics: enabled" << std::endl;
    }

    // Analytics endpoints
    if (analytics) {
        server.add_route(api_route("/analytics"), full_handler);
        std::cout << "  \U0001f4c8 GET  " << api_route("/analytics") << " - Analytics report" << std::endl;

        server.add_route(api_route("/analytics/gaps"), full_handler);
        std::cout << "  \U0001f4c8 GET  " << api_route("/analytics/gaps") << " - Knowledge gaps" << std::endl;

        server.add_route(api_route("/analytics/export"), full_handler);
        std::cout << "  \U0001f4c8 POST " << api_route("/analytics/export") << " - Export report" << std::endl;
    }

    // QA dedup endpoints (semantic deduplication)
    if (dedup) {
        server.add_route(api_route("/qa/dedup"), full_handler);
        std::cout << "  \U0001f504 POST " << api_route("/qa/dedup") << " - Find duplicates (semantic)" << std::endl;

        server.add_route(api_route("/qa/dedup/remove"), full_handler);
        std::cout << "  \U0001f504 POST " << api_route("/qa/dedup/remove") << " - Remove duplicates" << std::endl;
    }

    // Markdown import endpoints
    if (markdown) {
        server.add_route(api_route("/import/markdown"), full_handler);
        std::cout << "  \U0001f4dd POST " << api_route("/import/markdown") << " - Import Markdown" << std::endl;

        server.add_route(api_route("/import/history"), full_handler);
        std::cout << "  \U0001f4dd GET  " << api_route("/import/history") << " - Import history" << std::endl;
    }

    std::cout << "✅ All routes added" << std::endl;
}
