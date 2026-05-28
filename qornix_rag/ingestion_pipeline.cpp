/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "ingestion_pipeline.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cstring>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

#ifndef QORNIX_HAS_LIBZIP
#define QORNIX_HAS_LIBZIP 0
#endif

#ifndef QORNIX_HAS_XLSX
#define QORNIX_HAS_XLSX 0
#endif

#ifndef QORNIX_HAS_OPENXML
#define QORNIX_HAS_OPENXML 0
#endif

#if QORNIX_HAS_LIBZIP
#include <zip.h>
#endif

#if QORNIX_HAS_XLSX || QORNIX_HAS_OPENXML
#include <pugixml.hpp>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace qornix::rag {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string hashContent(const std::string& data) {
    unsigned long hash = 5381;
    for (char c : data) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::chrono::system_clock::time_point fileTimeToSystem(fs::file_time_type file_time) {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
}

bool looksBinary(const std::string& content) {
    const auto sample_size = std::min<size_t>(content.size(), 4096);
    for (size_t i = 0; i < sample_size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(content[i]);
        if (ch == 0) {
            return true;
        }
    }
    return false;
}

std::string collapseWhitespace(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    bool previous_space = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!previous_space) {
                output.push_back(' ');
            }
            previous_space = true;
        } else {
            output.push_back(static_cast<char>(ch));
            previous_space = false;
        }
    }
    if (!output.empty() && output.front() == ' ') {
        output.erase(output.begin());
    }
    if (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
    return output;
}

std::string trimWhitespace(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string normalizeExtractedTextPreservingLines(const std::string& value) {
    std::stringstream input(value);
    std::stringstream output;
    std::string line;
    bool previous_blank = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto trimmed = trimWhitespace(line);
        if (trimmed.empty()) {
            if (!previous_blank) {
                output << '\n';
            }
            previous_blank = true;
        } else {
            output << trimmed << '\n';
            previous_blank = false;
        }
    }
    return trimWhitespace(output.str());
}

std::string normalizePdfTextPreservingPages(const std::string& value, size_t& page_count) {
    std::stringstream output;
    size_t start = 0;
    size_t pos = 0;
    size_t page = 1;
    page_count = 0;
    while (pos <= value.size()) {
        if (pos == value.size() || value[pos] == '\f') {
            const auto page_text = normalizeExtractedTextPreservingLines(value.substr(start, pos - start));
            if (!page_text.empty()) {
                if (page_count > 0) {
                    output << "\f\n";
                }
                output << "Page " << page << ":\n" << page_text << "\n";
                ++page_count;
            }
            ++page;
            start = pos + 1;
        }
        ++pos;
    }
    return trimWhitespace(output.str());
}

std::string htmlEntityDecode(std::string value) {
    const std::pair<const char*, const char*> replacements[] = {
        {"&nbsp;", " "},
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&quot;", "\""},
        {"&#39;", "'"}
    };
    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    return value;
}

bool asciiEqualIgnoreCase(char lhs, char rhs) {
    return std::tolower(static_cast<unsigned char>(lhs)) ==
           std::tolower(static_cast<unsigned char>(rhs));
}

size_t findCaseInsensitive(const std::string& haystack,
                           const std::string& needle,
                           size_t start = 0) {
    if (needle.empty()) {
        return start <= haystack.size() ? start : std::string::npos;
    }
    if (needle.size() > haystack.size() || start > haystack.size() - needle.size()) {
        return std::string::npos;
    }
    for (size_t pos = start; pos <= haystack.size() - needle.size(); ++pos) {
        bool matched = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (!asciiEqualIgnoreCase(haystack[pos + i], needle[i])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return pos;
        }
    }
    return std::string::npos;
}

std::string htmlTagName(const std::string& html, size_t tag_start, size_t tag_end) {
    if (tag_start >= tag_end || html[tag_start] != '<') {
        return {};
    }
    size_t pos = tag_start + 1;
    while (pos < tag_end && (std::isspace(static_cast<unsigned char>(html[pos])) || html[pos] == '/')) {
        ++pos;
    }
    if (pos >= tag_end || html[pos] == '!' || html[pos] == '?') {
        return {};
    }
    std::string name;
    while (pos < tag_end) {
        const unsigned char ch = static_cast<unsigned char>(html[pos]);
        if (!std::isalnum(ch)) {
            break;
        }
        name.push_back(static_cast<char>(std::tolower(ch)));
        ++pos;
    }
    return name;
}

bool isHtmlBlockBreakTag(const std::string& tag_name) {
    static const std::set<std::string> tags = {
        "br", "p", "div", "section", "article", "header", "footer",
        "li", "tr", "table", "thead", "tbody", "tfoot", "ul", "ol",
        "h1", "h2", "h3", "h4", "h5", "h6"
    };
    return tags.count(tag_name) > 0;
}

std::string stripHtmlToText(const std::string& html) {
    std::string cleaned;
    cleaned.reserve(html.size());

    size_t pos = 0;
    while (pos < html.size()) {
        const auto tag_start = html.find('<', pos);
        if (tag_start == std::string::npos) {
            cleaned.append(html, pos, std::string::npos);
            break;
        }

        cleaned.append(html, pos, tag_start - pos);
        const auto tag_end = html.find('>', tag_start + 1);
        if (tag_end == std::string::npos) {
            cleaned.push_back(' ');
            break;
        }

        const auto tag_name = htmlTagName(html, tag_start, tag_end);
        if (tag_name == "script" || tag_name == "style") {
            const auto close_start = findCaseInsensitive(html, "</" + tag_name, tag_end + 1);
            if (close_start == std::string::npos) {
                cleaned.push_back(' ');
                break;
            }
            const auto close_end = html.find('>', close_start + 2 + tag_name.size());
            pos = close_end == std::string::npos ? html.size() : close_end + 1;
            cleaned.push_back(' ');
            continue;
        }

        cleaned.push_back(isHtmlBlockBreakTag(tag_name) ? '\n' : ' ');
        pos = tag_end + 1;
    }

    return collapseWhitespace(htmlEntityDecode(cleaned));
}

std::string extractHtmlTitle(const std::string& html) {
    const auto title_start = findCaseInsensitive(html, "<title");
    if (title_start == std::string::npos) {
        return {};
    }
    const auto open_end = html.find('>', title_start + 6);
    if (open_end == std::string::npos) {
        return {};
    }
    const auto close_start = findCaseInsensitive(html, "</title", open_end + 1);
    if (close_start == std::string::npos || close_start <= open_end) {
        return {};
    }
    return collapseWhitespace(htmlEntityDecode(html.substr(open_end + 1, close_start - open_end - 1)));
}


std::string boolString(bool value) {
    return value ? "true" : "false";
}

std::string truncateMetadataValue(const std::string& value, size_t limit = 512) {
    if (value.size() <= limit) {
        return value;
    }
    if (limit <= 3) {
        return value.substr(0, limit);
    }
    return value.substr(0, limit - 3) + "...";
}

void setMetadataIfNotEmpty(std::map<std::string, std::string>& metadata,
                           const std::string& key,
                           const std::string& value,
                           size_t limit = 512) {
    const auto cleaned = collapseWhitespace(value);
    if (!cleaned.empty()) {
        metadata[key] = truncateMetadataValue(cleaned, limit);
    }
}

std::string joinValues(const std::vector<std::string>& values,
                       const std::string& separator,
                       size_t max_values = 32,
                       size_t max_chars = 512) {
    std::string output;
    size_t emitted = 0;
    for (const auto& raw_value : values) {
        const auto value = collapseWhitespace(raw_value);
        if (value.empty()) {
            continue;
        }
        if (emitted > 0) {
            output += separator;
        }
        output += value;
        ++emitted;
        if (emitted >= max_values) {
            break;
        }
        if (output.size() >= max_chars) {
            break;
        }
    }
    return truncateMetadataValue(output, max_chars);
}

std::vector<std::string> splitLines(const std::string& value) {
    std::vector<std::string> lines;
    std::stringstream input(value);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> splitByChar(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::string part;
    std::stringstream input(value);
    while (std::getline(input, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

[[maybe_unused]] bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string normalizeMetadataKey(std::string value) {
    value = toLower(collapseWhitespace(value));
    for (char& ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            continue;
        }
        ch = '_';
    }
    std::string normalized;
    bool previous_underscore = false;
    for (char ch : value) {
        if (ch == '_') {
            if (!previous_underscore) {
                normalized.push_back(ch);
            }
            previous_underscore = true;
        } else {
            normalized.push_back(ch);
            previous_underscore = false;
        }
    }
    while (!normalized.empty() && normalized.front() == '_') {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }
    return normalized;
}

bool isLikelyNumber(const std::string& value) {
    const auto trimmed = trimWhitespace(value);
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    std::strtod(trimmed.c_str(), &end);
    return errno == 0 && end != trimmed.c_str() && *end == '\0';
}

bool isLikelyInteger(const std::string& value) {
    const auto trimmed = trimWhitespace(value);
    if (trimmed.empty()) {
        return false;
    }
    size_t pos = 0;
    if (trimmed[pos] == '+' || trimmed[pos] == '-') {
        ++pos;
    }
    if (pos >= trimmed.size()) {
        return false;
    }
    for (; pos < trimmed.size(); ++pos) {
        if (!std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
            return false;
        }
    }
    return true;
}

bool isLikelyBoolean(const std::string& value) {
    const auto lowered = toLower(trimWhitespace(value));
    return lowered == "true" || lowered == "false" || lowered == "yes" || lowered == "no";
}

bool isLikelyDate(const std::string& value) {
    const auto trimmed = trimWhitespace(value);
    return std::regex_match(trimmed, std::regex(R"(^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}(:\d{2})?)?$)")) ||
           std::regex_match(trimmed, std::regex(R"(^\d{1,2}[./-]\d{1,2}[./-]\d{2,4}$)"));
}

enum class IngestedCellKind {
    Empty,
    Text,
    Integer,
    Number,
    Boolean,
    Date,
    Formula,
    Mixed
};

std::string cellKindToString(IngestedCellKind kind) {
    switch (kind) {
        case IngestedCellKind::Empty: return "empty";
        case IngestedCellKind::Text: return "text";
        case IngestedCellKind::Integer: return "integer";
        case IngestedCellKind::Number: return "number";
        case IngestedCellKind::Boolean: return "boolean";
        case IngestedCellKind::Date: return "date";
        case IngestedCellKind::Formula: return "formula";
        case IngestedCellKind::Mixed: return "mixed";
    }
    return "mixed";
}

IngestedCellKind inferCellKind(const std::string& value) {
    const auto trimmed = trimWhitespace(value);
    if (trimmed.empty()) {
        return IngestedCellKind::Empty;
    }
    if (startsWith(trimmed, "=")) {
        return IngestedCellKind::Formula;
    }
    if (isLikelyBoolean(trimmed)) {
        return IngestedCellKind::Boolean;
    }
    if (isLikelyInteger(trimmed)) {
        return IngestedCellKind::Integer;
    }
    if (isLikelyNumber(trimmed)) {
        return IngestedCellKind::Number;
    }
    if (isLikelyDate(trimmed)) {
        return IngestedCellKind::Date;
    }
    return IngestedCellKind::Text;
}

IngestedCellKind mergeCellKind(IngestedCellKind current, IngestedCellKind next) {
    if (current == IngestedCellKind::Empty) {
        return next;
    }
    if (next == IngestedCellKind::Empty) {
        return current;
    }
    if (current == next) {
        return current;
    }
    if ((current == IngestedCellKind::Integer && next == IngestedCellKind::Number) ||
        (current == IngestedCellKind::Number && next == IngestedCellKind::Integer)) {
        return IngestedCellKind::Number;
    }
    return IngestedCellKind::Mixed;
}

std::string columnNameFromIndex(size_t index) {
    std::string name;
    ++index;
    while (index > 0) {
        const size_t rem = (index - 1) % 26;
        name.insert(name.begin(), static_cast<char>('A' + rem));
        index = (index - 1) / 26;
    }
    return name;
}

std::string cellCoordinate(size_t row_index, size_t column_index) {
    return columnNameFromIndex(column_index) + std::to_string(row_index + 1);
}

struct CommandCaptureResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
    std::string code;
    std::string message;
};

CommandCaptureResult runCommandCapture(const std::vector<std::string>& args,
                                       const std::string& code_prefix) {
#if defined(__unix__) || defined(__APPLE__)
    if (args.empty()) {
        return {false, -1, {}, code_prefix + "_empty_command", "Command arguments are empty"};
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {false, -1, {}, code_prefix + "_pipe_failed", "Could not create pipe"};
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {false, -1, {}, code_prefix + "_fork_failed", "Could not start command"};
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<size_t>(bytes));
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return {false, -1, output, code_prefix + "_wait_failed", "Could not wait for command"};
    }
    if (!WIFEXITED(status)) {
        return {false, -1, output, code_prefix + "_terminated", "Command terminated unexpectedly"};
    }

    const int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
        return {false, exit_code, output, code_prefix + "_unavailable", args.front() + " is not installed or not in PATH"};
    }
    if (exit_code != 0) {
        return {false, exit_code, output, code_prefix + "_failed", args.front() + " failed with exit code " + std::to_string(exit_code)};
    }
    return {true, exit_code, output, {}, {}};
#else
    (void)args;
    return {false, -1, {}, code_prefix + "_unavailable", "Command capture is not available on this platform"};
#endif
}

uint16_t readBigEndian16(const unsigned char* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t readBigEndian32(const unsigned char* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint32_t readLittleEndian32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

std::optional<std::pair<size_t, size_t>> parsePnmDimensions(const std::string& raw_content) {
    if (raw_content.size() < 3 || raw_content[0] != 'P') {
        return std::nullopt;
    }
    std::vector<std::string> tokens;
    std::string token;
    bool comment = false;
    for (char ch : raw_content) {
        if (comment) {
            if (ch == '\n' || ch == '\r') {
                comment = false;
            }
            continue;
        }
        if (ch == '#') {
            comment = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            if (tokens.size() >= 4) {
                break;
            }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty() && tokens.size() < 4) {
        tokens.push_back(token);
    }
    if (tokens.size() >= 3 && startsWith(tokens[0], "P")) {
        try {
            return std::make_pair(static_cast<size_t>(std::stoull(tokens[1])),
                                  static_cast<size_t>(std::stoull(tokens[2])));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<size_t, size_t>> detectImageDimensions(const std::string& raw_content,
                                                               const std::string& extension) {
    if (raw_content.size() >= 24 &&
        static_cast<unsigned char>(raw_content[0]) == 0x89 &&
        raw_content.substr(1, 3) == "PNG") {
        const auto* data = reinterpret_cast<const unsigned char*>(raw_content.data());
        return std::make_pair(static_cast<size_t>(readBigEndian32(data + 16)),
                              static_cast<size_t>(readBigEndian32(data + 20)));
    }

    if (raw_content.size() >= 10 && raw_content.substr(0, 3) == "GIF") {
        const auto* data = reinterpret_cast<const unsigned char*>(raw_content.data());
        return std::make_pair(static_cast<size_t>(data[6] | (data[7] << 8)),
                              static_cast<size_t>(data[8] | (data[9] << 8)));
    }

    if (raw_content.size() >= 26 && raw_content.substr(0, 2) == "BM") {
        const auto* data = reinterpret_cast<const unsigned char*>(raw_content.data());
        return std::make_pair(static_cast<size_t>(readLittleEndian32(data + 18)),
                              static_cast<size_t>(readLittleEndian32(data + 22)));
    }

    if (raw_content.size() >= 4 &&
        static_cast<unsigned char>(raw_content[0]) == 0xff &&
        static_cast<unsigned char>(raw_content[1]) == 0xd8) {
        size_t pos = 2;
        while (pos + 9 < raw_content.size()) {
            if (static_cast<unsigned char>(raw_content[pos]) != 0xff) {
                ++pos;
                continue;
            }
            const unsigned char marker = static_cast<unsigned char>(raw_content[pos + 1]);
            pos += 2;
            if (marker == 0xd8 || marker == 0xd9) {
                continue;
            }
            if (pos + 2 > raw_content.size()) {
                break;
            }
            const auto segment_length = readBigEndian16(reinterpret_cast<const unsigned char*>(raw_content.data() + pos));
            if (segment_length < 2 || pos + segment_length > raw_content.size()) {
                break;
            }
            if ((marker >= 0xc0 && marker <= 0xc3) ||
                (marker >= 0xc5 && marker <= 0xc7) ||
                (marker >= 0xc9 && marker <= 0xcb) ||
                (marker >= 0xcd && marker <= 0xcf)) {
                const auto* data = reinterpret_cast<const unsigned char*>(raw_content.data() + pos);
                return std::make_pair(static_cast<size_t>(readBigEndian16(data + 5)),
                                      static_cast<size_t>(readBigEndian16(data + 3)));
            }
            pos += segment_length;
        }
    }

    if (extension == ".pbm" || extension == ".pgm" || extension == ".ppm" || extension == ".pnm") {
        return parsePnmDimensions(raw_content);
    }
    return std::nullopt;
}

std::optional<int> detectJpegExifOrientation(const std::string& raw_content) {
    const auto exif_pos = raw_content.find("Exif\0\0", 0, 6);
    if (exif_pos == std::string::npos || exif_pos + 14 >= raw_content.size()) {
        return std::nullopt;
    }
    // Minimal diagnostic only: robust EXIF parsing is intentionally deferred to
    // dedicated image libraries. Presence is still recorded by the caller.
    return std::nullopt;
}

const std::set<std::string>& sourceExtensions() {
    static const std::set<std::string> extensions = {
        ".cpp", ".c", ".cc", ".cxx", ".c++",
        ".h", ".hpp", ".hxx", ".h++",
        ".java", ".cs", ".py", ".js", ".ts",
        ".go", ".rs", ".swift", ".kt", ".scala"
    };
    return extensions;
}

const std::map<std::string, std::pair<std::string, std::string>>& textExtensionMap() {
    static const std::map<std::string, std::pair<std::string, std::string>> mapping = {
        {".cpp", {"text/x-c++src", "C++"}},
        {".c", {"text/x-csrc", "C"}},
        {".cc", {"text/x-c++src", "C++"}},
        {".cxx", {"text/x-c++src", "C++"}},
        {".c++", {"text/x-c++src", "C++"}},
        {".h", {"text/x-chdr", "C/C++ Header"}},
        {".hpp", {"text/x-c++hdr", "C++ Header"}},
        {".hxx", {"text/x-c++hdr", "C++ Header"}},
        {".h++", {"text/x-c++hdr", "C++ Header"}},
        {".java", {"text/x-java-source", "Java"}},
        {".cs", {"text/x-csharp", "C#"}},
        {".py", {"text/x-python", "Python"}},
        {".js", {"text/javascript", "JavaScript"}},
        {".ts", {"text/typescript", "TypeScript"}},
        {".go", {"text/x-go", "Go"}},
        {".rs", {"text/x-rust", "Rust"}},
        {".swift", {"text/x-swift", "Swift"}},
        {".kt", {"text/x-kotlin", "Kotlin"}},
        {".scala", {"text/x-scala", "Scala"}},
        {".txt", {"text/plain", "Text"}},
        {".md", {"text/markdown", "Markdown"}},
        {".rst", {"text/x-rst", "reStructuredText"}},
        {".adoc", {"text/asciidoc", "AsciiDoc"}},
        {".yaml", {"application/x-yaml", "YAML"}},
        {".yml", {"application/x-yaml", "YAML"}},
        {".json", {"application/json", "JSON"}},
        {".xml", {"application/xml", "XML"}},
        {".html", {"text/html", "HTML"}},
        {".htm", {"text/html", "HTML"}},
        {".pdf", {"application/pdf", "PDF"}},
        {".docx", {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "DOCX"}},
        {".csv", {"text/csv", "CSV"}},
        {".xlsx", {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "XLSX"}},
        {".pptx", {"application/vnd.openxmlformats-officedocument.presentationml.presentation", "PPTX"}},
        {".png", {"image/png", "Image"}},
        {".jpg", {"image/jpeg", "Image"}},
        {".jpeg", {"image/jpeg", "Image"}},
        {".tif", {"image/tiff", "Image"}},
        {".tiff", {"image/tiff", "Image"}},
        {".bmp", {"image/bmp", "Image"}},
        {".webp", {"image/webp", "Image"}},
        {".pbm", {"image/x-portable-bitmap", "Image"}},
        {".pgm", {"image/x-portable-graymap", "Image"}},
        {".ppm", {"image/x-portable-pixmap", "Image"}},
        {".pnm", {"image/x-portable-anymap", "Image"}},
        {".toml", {"application/toml", "TOML"}},
        {".ini", {"text/plain", "INI"}},
        {".cfg", {"text/plain", "Config"}},
        {".conf", {"text/plain", "Config"}},
        {".cmake", {"text/x-cmake", "CMake"}}
    };
    return mapping;
}

std::vector<std::string> defaultExtensions() {
    std::vector<std::string> extensions;
    extensions.reserve(textExtensionMap().size());
    for (const auto& [extension, _] : textExtensionMap()) {
        extensions.push_back(extension);
    }
    return extensions;
}

class PlainTextParser final : public DocumentParser {
public:
    std::string id() const override { return "plain_text"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type != "text/html";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = raw_content;
        doc.document_type = detected.document_type;
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = doc.content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        return doc;
    }
};

class HtmlParser final : public DocumentParser {
public:
    std::string id() const override { return "html"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type == "text/html";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        auto title = extractHtmlTitle(raw_content);
        auto body = stripHtmlToText(raw_content);
        if (body.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_html", "HTML produced no text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = title.empty() ? body : (title + "\n\n" + body);
        doc.document_type = "html";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        if (!title.empty()) {
            doc.metadata["title"] = title;
        }
        return doc;
    }
};

char detectCsvDelimiter(const std::string& content) {
    const std::vector<char> candidates = {',', ';', '\t', '|'};
    std::map<char, size_t> counts;
    bool in_quotes = false;
    for (char ch : content) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes && (ch == '\n' || ch == '\r')) {
            break;
        }
        if (!in_quotes) {
            for (char candidate : candidates) {
                if (ch == candidate) {
                    counts[candidate]++;
                }
            }
        }
    }

    char best = ',';
    size_t best_count = 0;
    for (char candidate : candidates) {
        const auto count = counts[candidate];
        if (count > best_count) {
            best = candidate;
            best_count = count;
        }
    }
    return best;
}

std::vector<std::vector<std::string>> parseCsvRows(const std::string& content, char delimiter) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < content.size(); ++i) {
        const char ch = content[i];
        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < content.size() && content[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }

        if (ch == '"') {
            in_quotes = true;
        } else if (ch == delimiter) {
            row.push_back(field);
            field.clear();
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && i + 1 < content.size() && content[i + 1] == '\n') {
                ++i;
            }
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
        } else {
            field.push_back(ch);
        }
    }

    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

std::string joinFields(const std::vector<std::string>& fields, const std::string& separator) {
    std::string output;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            output += separator;
        }
        output += collapseWhitespace(fields[i]);
    }
    return output;
}

class CsvParser final : public DocumentParser {
public:
    std::string id() const override { return "csv"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".csv";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }
        if (looksBinary(raw_content)) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "binary_content", "File appears to be binary"});
            return std::nullopt;
        }

        const char delimiter = detectCsvDelimiter(raw_content);
        const auto rows = parseCsvRows(raw_content, delimiter);
        if (rows.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_csv", "CSV produced no rows"});
            return std::nullopt;
        }

        bool has_header = false;
        if (rows.size() > 1 && !rows.front().empty()) {
            size_t first_text_like = 0;
            size_t second_non_text = 0;
            for (size_t i = 0; i < rows.front().size(); ++i) {
                const auto first_kind = inferCellKind(rows.front()[i]);
                const auto second_kind = i < rows[1].size() ? inferCellKind(rows[1][i]) : IngestedCellKind::Empty;
                if (first_kind == IngestedCellKind::Text) {
                    ++first_text_like;
                }
                if (second_kind == IngestedCellKind::Integer || second_kind == IngestedCellKind::Number ||
                    second_kind == IngestedCellKind::Boolean || second_kind == IngestedCellKind::Date ||
                    second_kind == IngestedCellKind::Formula) {
                    ++second_non_text;
                }
            }
            has_header = first_text_like >= std::max<size_t>(1, rows.front().size() / 2) && second_non_text > 0;
        }

        size_t column_count = 0;
        size_t empty_cell_count = 0;
        size_t formula_cell_count = 0;
        std::map<IngestedCellKind, size_t> type_counts;
        std::vector<IngestedCellKind> column_types;
        std::vector<std::string> formula_cells;

        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const auto& row = rows[row_index];
            column_count = std::max(column_count, row.size());
            if (column_types.size() < row.size()) {
                column_types.resize(row.size(), IngestedCellKind::Empty);
            }
            const bool metric_row = !(has_header && row_index == 0);
            for (size_t column_index = 0; column_index < row.size(); ++column_index) {
                const auto kind = inferCellKind(row[column_index]);
                if (metric_row) {
                    type_counts[kind]++;
                    column_types[column_index] = mergeCellKind(column_types[column_index], kind);
                    if (kind == IngestedCellKind::Empty) {
                        ++empty_cell_count;
                    } else if (kind == IngestedCellKind::Formula) {
                        ++formula_cell_count;
                        formula_cells.push_back(cellCoordinate(row_index, column_index));
                    }
                }
            }
            if (metric_row) {
                for (size_t missing = row.size(); missing < column_count; ++missing) {
                    (void)missing;
                    ++empty_cell_count;
                    type_counts[IngestedCellKind::Empty]++;
                }
            }
        }

        std::vector<std::string> column_type_labels;
        for (size_t i = 0; i < column_types.size(); ++i) {
            column_type_labels.push_back(columnNameFromIndex(i) + ":" + cellKindToString(column_types[i]));
        }

        std::stringstream content;
        content << "CSV table: " << fs::path(path).filename().string() << "\n";
        content << "Columns: " << column_count << "\n";
        content << "Header inferred: " << boolString(has_header) << "\n";
        if (!column_type_labels.empty()) {
            content << "Column types: " << joinValues(column_type_labels, ", ") << "\n";
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            content << "Row " << (i + 1) << ": " << joinFields(rows[i], " | ") << "\n";
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content.str();
        doc.document_type = "csv";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["csv_delimiter"] = delimiter == '\t' ? "\\t" : std::string(1, delimiter);
        doc.metadata["csv_row_count"] = std::to_string(rows.size());
        doc.metadata["csv_column_count"] = std::to_string(column_count);
        doc.metadata["csv_has_header"] = boolString(has_header);
        doc.metadata["csv_empty_cell_count"] = std::to_string(empty_cell_count);
        doc.metadata["csv_formula_cell_count"] = std::to_string(formula_cell_count);
        doc.metadata["csv_column_types"] = joinValues(column_type_labels, ",");
        doc.metadata["csv_typed_cell_count_empty"] = std::to_string(type_counts[IngestedCellKind::Empty]);
        doc.metadata["csv_typed_cell_count_text"] = std::to_string(type_counts[IngestedCellKind::Text]);
        doc.metadata["csv_typed_cell_count_integer"] = std::to_string(type_counts[IngestedCellKind::Integer]);
        doc.metadata["csv_typed_cell_count_number"] = std::to_string(type_counts[IngestedCellKind::Number]);
        doc.metadata["csv_typed_cell_count_boolean"] = std::to_string(type_counts[IngestedCellKind::Boolean]);
        doc.metadata["csv_typed_cell_count_date"] = std::to_string(type_counts[IngestedCellKind::Date]);
        doc.metadata["csv_typed_cell_count_formula"] = std::to_string(type_counts[IngestedCellKind::Formula]);
        setMetadataIfNotEmpty(doc.metadata, "csv_formula_cells", joinValues(formula_cells, ","));
        if (!rows.empty()) {
            doc.metadata["csv_headers"] = joinFields(rows.front(), ",");
        }
        doc.metadata["structure_contract"] = "spreadsheet_rows_v2";
        doc.metadata["table_sheet_count"] = "1";
        doc.metadata["table_row_count"] = std::to_string(rows.size());
        doc.metadata["table_column_count"] = std::to_string(column_count);
        return doc;
    }
};

class PdfParser final : public DocumentParser {
public:
    std::string id() const override { return "pdf_pdftotext"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type == "application/pdf";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto extracted = extractText(path);
        if (!extracted.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, extracted.code, extracted.message});
            return std::nullopt;
        }

        size_t page_count = 0;
        const auto text = normalizePdfTextPreservingPages(extracted.text, page_count);
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_pdf_text", "PDF produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "pdf";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["pdf_text_extractor"] = "pdftotext";
        doc.metadata["pdf_text_extraction_status"] = "ok";
        doc.metadata["pdf_text_bytes"] = std::to_string(extracted.text.size());
        doc.metadata["structure_contract"] = "pdf_pages_v2";
        doc.metadata["pdf_page_count"] = std::to_string(page_count);
        enrichPdfMetadata(path, raw_content, doc.metadata);
        return doc;
    }

private:
    struct ExtractResult {
        bool ok = false;
        std::string text;
        std::string code;
        std::string message;
    };

    static std::string parsePdfLiteralValue(const std::string& raw_content, const std::string& key) {
        const auto pos = raw_content.find("/" + key);
        if (pos == std::string::npos) {
            return {};
        }
        const auto start = raw_content.find('(', pos);
        if (start == std::string::npos) {
            return {};
        }
        std::string value;
        bool escaped = false;
        for (size_t i = start + 1; i < raw_content.size(); ++i) {
            const char ch = raw_content[i];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == ')') {
                break;
            }
            if (std::isprint(static_cast<unsigned char>(ch)) || std::isspace(static_cast<unsigned char>(ch))) {
                value.push_back(ch);
            }
        }
        return collapseWhitespace(value);
    }

    static void parsePdfInfoOutput(const std::string& output,
                                   std::map<std::string, std::string>& metadata) {
        size_t page_box_count = 0;
        std::vector<std::string> page_boxes;
        for (const auto& raw_line : splitLines(output)) {
            const auto pos = raw_line.find(':');
            if (pos == std::string::npos) {
                continue;
            }
            const auto label = trimWhitespace(raw_line.substr(0, pos));
            const auto value = trimWhitespace(raw_line.substr(pos + 1));
            if (label.empty() || value.empty()) {
                continue;
            }
            const auto key = "pdf_info_" + normalizeMetadataKey(label);
            setMetadataIfNotEmpty(metadata, key, value, 384);
            const auto lowered = toLower(label);
            if (lowered.find("box") != std::string::npos || lowered.find("page size") != std::string::npos) {
                ++page_box_count;
                page_boxes.push_back(label + ": " + value);
            }
        }
        metadata["pdf_page_box_count"] = std::to_string(page_box_count);
        setMetadataIfNotEmpty(metadata, "pdf_page_boxes", joinValues(page_boxes, "; ", 24, 1024), 1024);
    }

    static void parsePdfDetachOutput(const std::string& output,
                                     std::map<std::string, std::string>& metadata) {
        std::vector<std::string> attachments;
        for (const auto& raw_line : splitLines(output)) {
            const auto line = trimWhitespace(raw_line);
            if (line.empty()) {
                continue;
            }
            std::smatch match;
            if (std::regex_search(line, match, std::regex(R"(^\d+:\s*(.+)$)")) && match.size() > 1) {
                attachments.push_back(collapseWhitespace(match[1].str()));
            }
        }
        metadata["pdf_attachment_count"] = std::to_string(attachments.size());
        setMetadataIfNotEmpty(metadata, "pdf_attachments", joinValues(attachments, ", ", 24, 1024), 1024);
    }

    static void parsePdfOutlineOutput(const std::string& output,
                                      std::map<std::string, std::string>& metadata) {
        std::vector<std::string> outlines;
        for (const auto& raw_line : splitLines(output)) {
            auto line = trimWhitespace(raw_line);
            if (line.empty()) {
                continue;
            }
            if (line.front() == '|' || line.front() == '+') {
                line = trimWhitespace(line.substr(1));
            }
            outlines.push_back(line);
        }
        metadata["pdf_outline_count"] = std::to_string(outlines.size());
        setMetadataIfNotEmpty(metadata, "pdf_outlines", joinValues(outlines, " | ", 32, 1024), 1024);
    }

    static void enrichPdfMetadata(const std::string& path,
                                  const std::string& raw_content,
                                  std::map<std::string, std::string>& metadata) {
        metadata["pdf_has_outline_tree"] = boolString(raw_content.find("/Outlines") != std::string::npos);
        metadata["pdf_has_embedded_files"] = boolString(raw_content.find("/EmbeddedFiles") != std::string::npos);
        metadata["pdf_has_xmp_metadata"] = boolString(raw_content.find("<?xpacket") != std::string::npos || raw_content.find("/Metadata") != std::string::npos);
        metadata["pdf_has_page_labels"] = boolString(raw_content.find("/PageLabels") != std::string::npos);
        metadata["pdf_has_struct_tree"] = boolString(raw_content.find("/StructTreeRoot") != std::string::npos);
        metadata["pdf_has_acroform"] = boolString(raw_content.find("/AcroForm") != std::string::npos);
        setMetadataIfNotEmpty(metadata, "pdf_raw_title", parsePdfLiteralValue(raw_content, "Title"));
        setMetadataIfNotEmpty(metadata, "pdf_raw_author", parsePdfLiteralValue(raw_content, "Author"));
        setMetadataIfNotEmpty(metadata, "pdf_raw_subject", parsePdfLiteralValue(raw_content, "Subject"));
        setMetadataIfNotEmpty(metadata, "pdf_raw_creator", parsePdfLiteralValue(raw_content, "Creator"));
        setMetadataIfNotEmpty(metadata, "pdf_raw_producer", parsePdfLiteralValue(raw_content, "Producer"));

        const auto info = runCommandCapture({"pdfinfo", "-box", path}, "pdfinfo");
        metadata["pdf_info_available"] = boolString(info.ok);
        metadata["pdf_info_exit_code"] = std::to_string(info.exit_code);
        if (info.ok) {
            parsePdfInfoOutput(info.output, metadata);
        } else {
            metadata["pdf_info_diagnostic_code"] = info.code;
            setMetadataIfNotEmpty(metadata, "pdf_info_diagnostic", info.message);
        }

        const auto detach = runCommandCapture({"pdfdetach", "-list", path}, "pdfdetach");
        metadata["pdf_attachments_available"] = boolString(detach.ok);
        if (detach.ok) {
            parsePdfDetachOutput(detach.output, metadata);
        } else {
            metadata["pdf_attachment_count"] = "0";
            metadata["pdf_attachments_diagnostic_code"] = detach.code;
        }

        const auto outline = runCommandCapture({"mutool", "show", path, "outline"}, "pdf_outline");
        metadata["pdf_outline_extractor_available"] = boolString(outline.ok);
        if (outline.ok) {
            parsePdfOutlineOutput(outline.output, metadata);
        } else {
            if (metadata.find("pdf_outline_count") == metadata.end()) {
                metadata["pdf_outline_count"] = "0";
            }
            metadata["pdf_outline_diagnostic_code"] = outline.code;
        }
    }

    static ExtractResult extractText(const std::string& path) {
        const auto result = runCommandCapture({"pdftotext", "-layout", "-enc", "UTF-8", path, "-"}, "pdf");
        if (!result.ok) {
            return {false, {}, result.code, result.message};
        }
        return {true, result.output, {}, {}};
    }
};

class ImageOcrParser final : public DocumentParser {
public:
    std::string id() const override { return "image_tesseract_ocr"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.mime_type.rfind("image/", 0) == 0;
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto extracted = extractText(path);
        if (!extracted.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, extracted.code, extracted.message});
            return std::nullopt;
        }

        const auto text = collapseWhitespace(extracted.text);
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_ocr_text", "Image OCR produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "image";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["image_ocr_engine"] = "tesseract";
        doc.metadata["image_ocr_language"] = extracted.language.empty() ? "default" : extracted.language;
        doc.metadata["image_ocr_detected_language"] = extracted.language.empty() ? "unknown" : extracted.language;
        doc.metadata["image_ocr_confidence_avg"] = extracted.average_confidence < 0.0
            ? "unknown"
            : formatConfidence(extracted.average_confidence);
        doc.metadata["image_ocr_word_count"] = std::to_string(extracted.word_count);
        doc.metadata["image_ocr_line_count"] = std::to_string(extracted.line_count);
        doc.metadata["image_ocr_diagnostic"] = extracted.diagnostic;
        doc.metadata["structure_contract"] = "ocr_regions_v2";
        doc.metadata["ocr_region_count"] = "1";
        doc.metadata["ocr_region_0"] = extracted.region.empty() ? "full_image" : extracted.region;
        doc.metadata["ocr_region_0_confidence"] = extracted.average_confidence < 0.0
            ? "unknown"
            : formatConfidence(extracted.average_confidence);
        doc.metadata["ocr_region_0_text_length"] = std::to_string(text.size());
        doc.metadata["ocr_caption"] = truncateMetadataValue(text, 180);
        enrichImageMetadata(raw_content, detected.extension, doc.metadata);
        return doc;
    }

private:
    struct ExtractResult {
        bool ok = false;
        std::string text;
        std::string code;
        std::string message;
        std::string diagnostic = "plain_text";
        std::string language;
        std::string region;
        double average_confidence = -1.0;
        size_t word_count = 0;
        size_t line_count = 0;
    };

    static std::string formatConfidence(double value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << value;
        return ss.str();
    }

    static std::optional<ExtractResult> parseTesseractTsv(const std::string& output) {
        const auto lines = splitLines(output);
        if (lines.size() < 2 || lines.front().find("conf") == std::string::npos) {
            return std::nullopt;
        }

        ExtractResult parsed;
        parsed.ok = true;
        parsed.diagnostic = "tsv";
        int min_left = std::numeric_limits<int>::max();
        int min_top = std::numeric_limits<int>::max();
        int max_right = 0;
        int max_bottom = 0;
        double conf_sum = 0.0;
        size_t conf_count = 0;
        size_t line_count = 0;
        std::string previous_line_key;
        std::vector<std::string> words;

        for (size_t i = 1; i < lines.size(); ++i) {
            const auto cols = splitByChar(lines[i], '\t');
            if (cols.size() < 12) {
                continue;
            }
            const auto text = collapseWhitespace(cols[11]);
            if (text.empty()) {
                continue;
            }
            try {
                const int left = std::stoi(cols[6]);
                const int top = std::stoi(cols[7]);
                const int width = std::stoi(cols[8]);
                const int height = std::stoi(cols[9]);
                const double conf = std::stod(cols[10]);
                min_left = std::min(min_left, left);
                min_top = std::min(min_top, top);
                max_right = std::max(max_right, left + width);
                max_bottom = std::max(max_bottom, top + height);
                if (conf >= 0.0) {
                    conf_sum += conf;
                    ++conf_count;
                }
            } catch (...) {
                // Keep the extracted text even when TSV coordinates are malformed.
            }
            const std::string line_key = cols[1] + ":" + cols[2] + ":" + cols[3] + ":" + cols[4];
            if (line_key != previous_line_key) {
                ++line_count;
                previous_line_key = line_key;
            }
            words.push_back(text);
        }

        if (words.empty()) {
            return std::nullopt;
        }
        parsed.text = joinValues(words, " ", words.size(), 16384);
        parsed.word_count = words.size();
        parsed.line_count = line_count;
        if (conf_count > 0) {
            parsed.average_confidence = conf_sum / static_cast<double>(conf_count);
        }
        if (min_left != std::numeric_limits<int>::max()) {
            parsed.region = "bbox:" + std::to_string(min_left) + "," + std::to_string(min_top) + "," +
                            std::to_string(max_right - min_left) + "," + std::to_string(max_bottom - min_top);
        }
        return parsed;
    }

    static ExtractResult extractText(const std::string& path) {
        const auto tsv = runCommandCapture({"tesseract", path, "stdout", "tsv"}, "image_ocr");
        if (tsv.ok) {
            auto parsed = parseTesseractTsv(tsv.output);
            if (parsed) {
                return *parsed;
            }
        }

        const auto plain = runCommandCapture({"tesseract", path, "stdout"}, "image_ocr");
        if (!plain.ok) {
            ExtractResult result;
            result.ok = false;
            result.code = plain.code;
            result.message = plain.message;
            return result;
        }
        ExtractResult result;
        result.ok = true;
        result.text = plain.output;
        result.diagnostic = tsv.ok ? "tsv_parse_empty_plain_fallback" : "plain_fallback";
        result.line_count = splitLines(plain.output).size();
        const auto collapsed_plain = collapseWhitespace(plain.output);
        result.word_count = collapsed_plain.empty() ? 0 : splitByChar(collapsed_plain, ' ').size();
        return result;
    }

    static void enrichImageMetadata(const std::string& raw_content,
                                    const std::string& extension,
                                    std::map<std::string, std::string>& metadata) {
        const auto dimensions = detectImageDimensions(raw_content, extension);
        metadata["image_dimensions_available"] = boolString(dimensions.has_value());
        if (dimensions) {
            metadata["image_width"] = std::to_string(dimensions->first);
            metadata["image_height"] = std::to_string(dimensions->second);
            metadata["image_dimensions"] = std::to_string(dimensions->first) + "x" + std::to_string(dimensions->second);
        }
        const bool exif_present = raw_content.find("Exif\0\0", 0, 6) != std::string::npos;
        metadata["image_exif_present"] = boolString(exif_present);
        const auto orientation = detectJpegExifOrientation(raw_content);
        if (orientation) {
            metadata["image_exif_orientation"] = std::to_string(*orientation);
        }
    }
};

std::string stripXmlTagsToText(const std::string& xml) {
    std::string text = std::regex_replace(xml, std::regex("<w:(p|br|cr|tab)[^>]*/?>", std::regex_constants::icase), "\n");
    text = std::regex_replace(text, std::regex("</w:p>", std::regex_constants::icase), "\n");
    text = std::regex_replace(text, std::regex("<[^>]+>", std::regex_constants::icase), " ");
    return collapseWhitespace(htmlEntityDecode(text));
}

struct ZipEntryResult {
    bool ok = false;
    std::string data;
    std::string code;
    std::string message;
};

ZipEntryResult readZipEntry(const std::string& archive_path, const std::string& entry_path, const std::string& code_prefix) {
#if QORNIX_HAS_LIBZIP
    int zip_error = 0;
    zip_t* archive = zip_open(archive_path.c_str(), ZIP_RDONLY, &zip_error);
    if (!archive) {
        return {false, {}, code_prefix + "_open_failed", "Could not open ZIP archive"};
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, entry_path.c_str(), 0, &stat) != 0) {
        zip_close(archive);
        return {false, {}, code_prefix + "_entry_missing", "ZIP archive does not contain " + entry_path};
    }

    zip_file_t* file = zip_fopen(archive, entry_path.c_str(), 0);
    if (!file) {
        zip_close(archive);
        return {false, {}, code_prefix + "_entry_open_failed", "Could not open " + entry_path};
    }

    std::string data;
    data.resize(static_cast<size_t>(stat.size));
    zip_int64_t total_read = 0;
    while (total_read < static_cast<zip_int64_t>(data.size())) {
        const auto bytes = zip_fread(file,
                                     data.data() + total_read,
                                     static_cast<zip_uint64_t>(data.size() - total_read));
        if (bytes < 0) {
            zip_fclose(file);
            zip_close(archive);
            return {false, {}, code_prefix + "_entry_read_failed", "Could not read " + entry_path};
        }
        if (bytes == 0) {
            break;
        }
        total_read += bytes;
    }

    zip_fclose(file);
    zip_close(archive);
    data.resize(static_cast<size_t>(total_read));
    return {true, data, {}, {}};
#else
    (void)archive_path;
    (void)entry_path;
    return {false, {}, code_prefix + "_parser_unavailable", "ZIP archive parsing requires libzip development libraries"};
#endif
}


#if QORNIX_HAS_LIBZIP
std::vector<std::string> listZipEntries(const std::string& archive_path) {
    std::vector<std::string> entries;
#if QORNIX_HAS_LIBZIP
    int zip_error = 0;
    zip_t* archive = zip_open(archive_path.c_str(), ZIP_RDONLY, &zip_error);
    if (!archive) {
        return entries;
    }
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(std::max<zip_int64_t>(0, count)); ++i) {
        const char* name = zip_get_name(archive, i, 0);
        if (name) {
            entries.emplace_back(name);
        }
    }
    zip_close(archive);
#else
    (void)archive_path;
#endif
    return entries;
}

std::vector<std::string> filterZipEntries(const std::vector<std::string>& entries,
                                          const std::string& prefix,
                                          const std::string& suffix = {}) {
    std::vector<std::string> filtered;
    for (const auto& entry : entries) {
        if (!startsWith(entry, prefix)) {
            continue;
        }
        if (!suffix.empty() && !endsWith(entry, suffix)) {
            continue;
        }
        filtered.push_back(entry);
    }
    std::sort(filtered.begin(), filtered.end());
    return filtered;
}
#endif

#if QORNIX_HAS_XLSX || QORNIX_HAS_OPENXML
void appendNodeText(pugi::xml_node node, std::string& text) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
        } else {
            appendNodeText(child, text);
        }
    }
}


std::string collectNodeText(pugi::xml_node node) {
    std::string text;
    appendNodeText(node, text);
    return collapseWhitespace(text);
}

bool xmlNameMatches(const char* raw_name, const std::string& local_name) {
    const std::string name = raw_name ? raw_name : "";
    return name == local_name || endsWith(name, ":" + local_name);
}

bool xmlNodeIs(pugi::xml_node node, const std::string& local_name) {
    return xmlNameMatches(node.name(), local_name);
}

std::string xmlAttributeValue(pugi::xml_node node, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        auto attr = node.attribute(name.c_str());
        if (attr) {
            return attr.as_string();
        }
    }
    for (auto attr : node.attributes()) {
        for (const auto& name : names) {
            if (xmlNameMatches(attr.name(), name)) {
                return attr.as_string();
            }
        }
    }
    return {};
}

pugi::xml_node firstChildByLocalName(pugi::xml_node node, const std::string& local_name) {
    for (pugi::xml_node child : node.children()) {
        if (xmlNodeIs(child, local_name)) {
            return child;
        }
    }
    return {};
}

void collectDescendantsByLocalName(pugi::xml_node node,
                                   const std::string& local_name,
                                   std::vector<pugi::xml_node>& out) {
    for (pugi::xml_node child : node.children()) {
        if (xmlNodeIs(child, local_name)) {
            out.push_back(child);
        }
        collectDescendantsByLocalName(child, local_name, out);
    }
}

std::vector<pugi::xml_node> descendantsByLocalName(pugi::xml_node node, const std::string& local_name) {
    std::vector<pugi::xml_node> out;
    collectDescendantsByLocalName(node, local_name, out);
    return out;
}

std::string xmlChildTextByLocalName(pugi::xml_node node, const std::string& local_name) {
    for (auto child : node.children()) {
        if (xmlNodeIs(child, local_name)) {
            return collectNodeText(child);
        }
    }
    return {};
}

std::string normalizeZipPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> stack;
    for (const auto& part : splitByChar(path, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!stack.empty()) {
                stack.pop_back();
            }
            continue;
        }
        stack.push_back(part);
    }
    return joinValues(stack, "/", stack.size(), 2048);
}

std::string resolveOpenXmlTarget(const std::string& base_part, const std::string& target) {
    if (target.empty()) {
        return {};
    }
    if (target.front() == '/') {
        return normalizeZipPath(target.substr(1));
    }
    const auto slash = base_part.find_last_of('/');
    const auto base_dir = slash == std::string::npos ? std::string{} : base_part.substr(0, slash + 1);
    return normalizeZipPath(base_dir + target);
}

std::string relationshipPartPath(const std::string& part_path) {
    const auto slash = part_path.find_last_of('/');
    if (slash == std::string::npos) {
        return "_rels/" + part_path + ".rels";
    }
    return part_path.substr(0, slash + 1) + "_rels/" + part_path.substr(slash + 1) + ".rels";
}

struct OpenXmlRelationship {
    std::string id;
    std::string type;
    std::string target;
};

std::vector<OpenXmlRelationship> parseOpenXmlRelationships(const std::string& rels_xml,
                                                           const std::string& base_part) {
    std::vector<OpenXmlRelationship> relationships;
    pugi::xml_document rels;
    if (!rels.load_string(rels_xml.c_str())) {
        return relationships;
    }
    for (pugi::xml_node rel : rels.child("Relationships").children("Relationship")) {
        OpenXmlRelationship relationship;
        relationship.id = rel.attribute("Id").as_string();
        relationship.type = rel.attribute("Type").as_string();
        relationship.target = resolveOpenXmlTarget(base_part, rel.attribute("Target").as_string());
        relationships.push_back(std::move(relationship));
    }
    return relationships;
}

std::string coreProperty(const std::string& xml, const std::string& local_name) {
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str())) {
        return {};
    }
    auto nodes = descendantsByLocalName(doc, local_name);
    if (nodes.empty()) {
        return {};
    }
    return collectNodeText(nodes.front());
}

std::vector<std::string> parseSharedStrings(const std::string& xml) {
    std::vector<std::string> shared_strings;
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str())) {
        return shared_strings;
    }
    for (pugi::xml_node si : doc.child("sst").children("si")) {
        std::string value;
        appendNodeText(si, value);
        shared_strings.push_back(collapseWhitespace(value));
    }
    return shared_strings;
}

std::string resolveWorkbookTarget(std::string target) {
    if (target.empty()) {
        return target;
    }
    if (target.front() == '/') {
        target.erase(target.begin());
        return target;
    }
    if (target.rfind("xl/", 0) == 0) {
        return target;
    }
    return "xl/" + target;
}

std::map<std::string, std::string> parseSheetNames(const std::string& workbook_xml, const std::string& rels_xml) {
    std::map<std::string, std::string> relationship_targets;
    pugi::xml_document rels;
    if (rels.load_string(rels_xml.c_str())) {
        for (pugi::xml_node rel : rels.child("Relationships").children("Relationship")) {
            relationship_targets[rel.attribute("Id").as_string()] = resolveWorkbookTarget(rel.attribute("Target").as_string());
        }
    }

    std::map<std::string, std::string> sheet_names;
    pugi::xml_document workbook;
    if (!workbook.load_string(workbook_xml.c_str())) {
        return sheet_names;
    }
    for (pugi::xml_node sheet : workbook.child("workbook").child("sheets").children("sheet")) {
        const std::string rel_id = sheet.attribute("r:id").as_string();
        const std::string name = sheet.attribute("name").as_string();
        auto target_it = relationship_targets.find(rel_id);
        if (target_it != relationship_targets.end() && !name.empty()) {
            sheet_names[target_it->second] = name;
        }
    }
    return sheet_names;
}

std::string cellValue(pugi::xml_node cell, const std::vector<std::string>& shared_strings) {
    const std::string type = cell.attribute("t").as_string();
    if (type == "inlineStr") {
        std::string value;
        appendNodeText(cell.child("is"), value);
        return collapseWhitespace(value);
    }

    const std::string raw = cell.child("v").text().as_string();
    if (type == "s") {
        try {
            const auto index = static_cast<size_t>(std::stoul(raw));
            if (index < shared_strings.size()) {
                return shared_strings[index];
            }
        } catch (...) {
            return {};
        }
    }
    return collapseWhitespace(raw);
}

std::string resolvePresentationTarget(std::string target) {
    if (target.empty()) {
        return target;
    }
    if (target.front() == '/') {
        target.erase(target.begin());
        return target;
    }
    if (target.rfind("ppt/", 0) == 0) {
        return target;
    }
    return "ppt/" + target;
}

std::vector<std::string> parseSlidePaths(const std::string& presentation_xml, const std::string& rels_xml) {
    std::map<std::string, std::string> relationship_targets;
    pugi::xml_document rels;
    if (rels.load_string(rels_xml.c_str())) {
        for (pugi::xml_node rel : rels.child("Relationships").children("Relationship")) {
            relationship_targets[rel.attribute("Id").as_string()] = resolvePresentationTarget(rel.attribute("Target").as_string());
        }
    }

    std::vector<std::string> slide_paths;
    pugi::xml_document presentation;
    if (!presentation.load_string(presentation_xml.c_str())) {
        return slide_paths;
    }
    for (pugi::xml_node slide : presentation.child("p:presentation").child("p:sldIdLst").children("p:sldId")) {
        const std::string rel_id = slide.attribute("r:id").as_string();
        auto target_it = relationship_targets.find(rel_id);
        if (target_it != relationship_targets.end()) {
            slide_paths.push_back(target_it->second);
        }
    }
    return slide_paths;
}

void collectPresentationText(pugi::xml_node node, std::vector<std::string>& values) {
    for (pugi::xml_node child : node.children()) {
        const std::string name = child.name();
        if (name == "a:t" || name == "t") {
            values.push_back(collapseWhitespace(child.text().as_string()));
        } else {
            collectPresentationText(child, values);
        }
    }
}
#endif

class DocxParser final : public DocumentParser {
public:
    std::string id() const override { return "docx_libzip"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".docx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

        const auto document_xml = readZipEntry(path, "word/document.xml", "docx_document");
        if (!document_xml.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, document_xml.code, document_xml.message});
            return std::nullopt;
        }

        std::map<std::string, std::string> rich_metadata;
        std::string text;
#if QORNIX_HAS_OPENXML
        text = parseRichDocument(path, document_xml.data, rich_metadata);
#else
        text = stripXmlTagsToText(document_xml.data);
        rich_metadata["docx_rich_metadata_available"] = "false";
        rich_metadata["docx_rich_metadata_diagnostic"] = "pugixml_unavailable";
#endif
        if (text.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_docx_text", "DOCX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = text;
        doc.document_type = "docx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["docx_archive_backend"] = "libzip";
        doc.metadata["docx_xml_parser"] = rich_metadata["docx_rich_metadata_available"] == "false" ? "regex_fallback" : "pugixml";
        doc.metadata.insert(rich_metadata.begin(), rich_metadata.end());
        return doc;
    }

private:
#if QORNIX_HAS_OPENXML
    static bool isHeadingStyle(const std::string& style) {
        const auto lowered = toLower(style);
        return startsWith(lowered, "heading") || startsWith(lowered, "tocheading") ||
               lowered.find("заголов") != std::string::npos;
    }

    static std::string paragraphStyle(pugi::xml_node paragraph) {
        auto properties = firstChildByLocalName(paragraph, "pPr");
        if (!properties) {
            return {};
        }
        auto style = firstChildByLocalName(properties, "pStyle");
        if (!style) {
            return {};
        }
        return xmlAttributeValue(style, {"w:val", "val"});
    }

    static std::string parseParagraph(pugi::xml_node paragraph) {
        return collectNodeText(paragraph);
    }

    static void appendParagraphLine(std::stringstream& content,
                                    const std::string& text,
                                    const std::string& style,
                                    std::vector<std::string>& headings,
                                    size_t& paragraph_count) {
        if (text.empty()) {
            return;
        }
        ++paragraph_count;
        if (isHeadingStyle(style)) {
            headings.push_back(text);
            content << "Heading: " << text << "\n";
        } else {
            content << text << "\n";
        }
    }

    static void parseTables(pugi::xml_node table,
                            std::stringstream& content,
                            size_t table_index,
                            size_t& table_row_count,
                            size_t& table_cell_count) {
        size_t row_index = 0;
        for (auto row : table.children()) {
            if (!xmlNodeIs(row, "tr")) {
                continue;
            }
            ++row_index;
            ++table_row_count;
            std::vector<std::string> cells;
            for (auto cell : row.children()) {
                if (!xmlNodeIs(cell, "tc")) {
                    continue;
                }
                ++table_cell_count;
                cells.push_back(collectNodeText(cell));
            }
            if (!cells.empty()) {
                content << "Table " << table_index << " Row " << row_index << ": " << joinValues(cells, " | ", cells.size(), 2048) << "\n";
            }
        }
    }

    static size_t appendNotesOrComments(const std::string& archive_path,
                                        const std::string& entry,
                                        const std::string& local_node_name,
                                        const std::string& label,
                                        std::stringstream& content,
                                        std::vector<std::string>& samples) {
        const auto xml = readZipEntry(archive_path, entry, "docx_" + normalizeMetadataKey(label));
        if (!xml.ok) {
            return 0;
        }
        pugi::xml_document doc;
        if (!doc.load_string(xml.data.c_str())) {
            return 0;
        }
        size_t count = 0;
        for (auto node : descendantsByLocalName(doc, local_node_name)) {
            const auto id = xmlAttributeValue(node, {"w:id", "id"});
            if (id == "-1" || id == "0") {
                continue;
            }
            const auto text = collectNodeText(node);
            if (text.empty()) {
                continue;
            }
            ++count;
            samples.push_back(text);
            content << label << " " << count << ": " << text << "\n";
        }
        return count;
    }

    static void parseStyles(const std::string& archive_path,
                            std::map<std::string, std::string>& metadata) {
        const auto styles = readZipEntry(archive_path, "word/styles.xml", "docx_styles");
        if (!styles.ok) {
            metadata["docx_styles_available"] = "false";
            metadata["docx_style_count"] = "0";
            return;
        }
        pugi::xml_document doc;
        if (!doc.load_string(styles.data.c_str())) {
            metadata["docx_styles_available"] = "false";
            metadata["docx_style_count"] = "0";
            return;
        }
        std::vector<std::string> style_names;
        for (auto style : descendantsByLocalName(doc, "style")) {
            const auto id = xmlAttributeValue(style, {"w:styleId", "styleId"});
            std::string name;
            for (auto child : style.children()) {
                if (xmlNodeIs(child, "name")) {
                    name = xmlAttributeValue(child, {"w:val", "val"});
                    break;
                }
            }
            style_names.push_back(name.empty() ? id : name);
        }
        metadata["docx_styles_available"] = "true";
        metadata["docx_style_count"] = std::to_string(style_names.size());
        setMetadataIfNotEmpty(metadata, "docx_style_names", joinValues(style_names, ", ", 32, 1024), 1024);
    }

    static void parseDocumentProperties(const std::string& archive_path,
                                        std::map<std::string, std::string>& metadata) {
        const auto core = readZipEntry(archive_path, "docProps/core.xml", "docx_core_props");
        if (core.ok) {
            setMetadataIfNotEmpty(metadata, "docx_title", coreProperty(core.data, "title"));
            setMetadataIfNotEmpty(metadata, "docx_subject", coreProperty(core.data, "subject"));
            setMetadataIfNotEmpty(metadata, "docx_creator", coreProperty(core.data, "creator"));
            setMetadataIfNotEmpty(metadata, "docx_last_modified_by", coreProperty(core.data, "lastModifiedBy"));
            setMetadataIfNotEmpty(metadata, "docx_created", coreProperty(core.data, "created"));
            setMetadataIfNotEmpty(metadata, "docx_modified", coreProperty(core.data, "modified"));
            setMetadataIfNotEmpty(metadata, "docx_keywords", coreProperty(core.data, "keywords"));
        }
        const auto app = readZipEntry(archive_path, "docProps/app.xml", "docx_app_props");
        if (app.ok) {
            setMetadataIfNotEmpty(metadata, "docx_application", coreProperty(app.data, "Application"));
            setMetadataIfNotEmpty(metadata, "docx_company", coreProperty(app.data, "Company"));
            setMetadataIfNotEmpty(metadata, "docx_manager", coreProperty(app.data, "Manager"));
            setMetadataIfNotEmpty(metadata, "docx_pages", coreProperty(app.data, "Pages"));
            setMetadataIfNotEmpty(metadata, "docx_words", coreProperty(app.data, "Words"));
        }
    }

    static std::string parseRichDocument(const std::string& archive_path,
                                         const std::string& document_xml,
                                         std::map<std::string, std::string>& metadata) {
        metadata["docx_rich_metadata_available"] = "true";
        pugi::xml_document xml;
        if (!xml.load_string(document_xml.c_str())) {
            metadata["docx_rich_metadata_available"] = "false";
            metadata["docx_rich_metadata_diagnostic"] = "document_xml_parse_failed";
            return stripXmlTagsToText(document_xml);
        }

        std::stringstream content;
        size_t paragraph_count = 0;
        size_t table_count = 0;
        size_t table_row_count = 0;
        size_t table_cell_count = 0;
        std::vector<std::string> headings;

        auto body_nodes = descendantsByLocalName(xml, "body");
        pugi::xml_node body = body_nodes.empty() ? xml : body_nodes.front();
        for (auto child : body.children()) {
            if (xmlNodeIs(child, "p")) {
                appendParagraphLine(content, parseParagraph(child), paragraphStyle(child), headings, paragraph_count);
            } else if (xmlNodeIs(child, "tbl")) {
                ++table_count;
                parseTables(child, content, table_count, table_row_count, table_cell_count);
            }
        }

        std::vector<std::string> footnotes;
        std::vector<std::string> endnotes;
        std::vector<std::string> comments;
        const size_t footnote_count = appendNotesOrComments(archive_path, "word/footnotes.xml", "footnote", "Footnote", content, footnotes);
        const size_t endnote_count = appendNotesOrComments(archive_path, "word/endnotes.xml", "endnote", "Endnote", content, endnotes);
        const size_t comment_count = appendNotesOrComments(archive_path, "word/comments.xml", "comment", "Comment", content, comments);

        metadata["docx_paragraph_count"] = std::to_string(paragraph_count);
        metadata["docx_heading_count"] = std::to_string(headings.size());
        metadata["docx_table_count"] = std::to_string(table_count);
        metadata["docx_table_row_count"] = std::to_string(table_row_count);
        metadata["docx_table_cell_count"] = std::to_string(table_cell_count);
        metadata["docx_footnote_count"] = std::to_string(footnote_count);
        metadata["docx_endnote_count"] = std::to_string(endnote_count);
        metadata["docx_comment_count"] = std::to_string(comment_count);
        setMetadataIfNotEmpty(metadata, "docx_headings", joinValues(headings, " | ", 32, 2048), 2048);
        setMetadataIfNotEmpty(metadata, "docx_footnotes", joinValues(footnotes, " | ", 16, 1024), 1024);
        setMetadataIfNotEmpty(metadata, "docx_endnotes", joinValues(endnotes, " | ", 16, 1024), 1024);
        setMetadataIfNotEmpty(metadata, "docx_comments", joinValues(comments, " | ", 16, 1024), 1024);
        metadata["structure_contract"] = "docx_blocks_v1";
        parseStyles(archive_path, metadata);
        parseDocumentProperties(archive_path, metadata);
        return normalizeExtractedTextPreservingLines(content.str());
    }
#endif
};

class XlsxParser final : public DocumentParser {
public:
    std::string id() const override { return "xlsx_libzip_pugixml"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".xlsx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

#if QORNIX_HAS_XLSX
        const auto workbook = readZipEntry(path, "xl/workbook.xml", "xlsx_workbook");
        if (!workbook.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, workbook.code, workbook.message});
            return std::nullopt;
        }

        const auto rels = readZipEntry(path, "xl/_rels/workbook.xml.rels", "xlsx_workbook_rels");
        const auto shared = readZipEntry(path, "xl/sharedStrings.xml", "xlsx_shared_strings");
        const auto shared_strings = shared.ok ? parseSharedStrings(shared.data) : std::vector<std::string>{};
        const auto sheet_names = rels.ok ? parseSheetNames(workbook.data, rels.data) : std::map<std::string, std::string>{};
        const auto zip_entries = listZipEntries(path);

        std::stringstream text;
        size_t sheet_count = 0;
        size_t row_count = 0;
        size_t cell_count = 0;
        size_t formula_count = 0;
        size_t merged_cell_count = 0;
        size_t styled_cell_count = 0;
        size_t max_column_count = 0;
        std::vector<std::string> names;
        std::vector<std::string> dimensions;
        std::vector<std::string> formula_cells;
        std::vector<std::string> merged_ranges;
        std::map<IngestedCellKind, size_t> type_counts;
        std::set<std::string> style_refs;

        for (const auto& [entry_path, sheet_name] : sheet_names) {
            const auto sheet = readZipEntry(path, entry_path, "xlsx_sheet");
            if (!sheet.ok) {
                continue;
            }

            pugi::xml_document sheet_doc;
            if (!sheet_doc.load_string(sheet.data.c_str())) {
                continue;
            }

            ++sheet_count;
            names.push_back(sheet_name);
            text << "Sheet: " << sheet_name << "\n";
            auto worksheet = sheet_doc.child("worksheet");
            const auto dimension = worksheet.child("dimension").attribute("ref").as_string();
            if (std::strlen(dimension) > 0) {
                dimensions.push_back(sheet_name + ":" + dimension);
            }

            for (pugi::xml_node merge : worksheet.child("mergeCells").children("mergeCell")) {
                const auto ref = merge.attribute("ref").as_string();
                if (std::strlen(ref) > 0) {
                    ++merged_cell_count;
                    merged_ranges.push_back(sheet_name + ":" + ref);
                }
            }

            for (pugi::xml_node row : worksheet.child("sheetData").children("row")) {
                std::vector<std::string> fields;
                size_t row_column_count = 0;
                for (pugi::xml_node cell : row.children("c")) {
                    const std::string ref = cell.attribute("r").as_string();
                    const std::string style_ref = cell.attribute("s").as_string();
                    const std::string formula = cell.child("f").text().as_string();
                    std::string value = cellValue(cell, shared_strings);
                    auto kind = inferXlsxCellKind(cell, value, formula);
                    type_counts[kind]++;
                    if (!style_ref.empty()) {
                        ++styled_cell_count;
                        style_refs.insert(style_ref);
                    }
                    if (!formula.empty()) {
                        ++formula_count;
                        formula_cells.push_back((ref.empty() ? (sheet_name + "!?") : (sheet_name + "!" + ref)) + "=" + formula);
                        value = value.empty() ? ("=" + formula) : ("=" + formula + " -> " + value);
                    }
                    fields.push_back(value);
                    ++cell_count;
                    ++row_column_count;
                }
                max_column_count = std::max(max_column_count, row_column_count);
                if (!fields.empty()) {
                    ++row_count;
                    text << "Row " << row.attribute("r").as_string() << ": " << joinFields(fields, " | ") << "\n";
                }
            }
            text << "\n";
        }

        std::map<std::string, std::string> rich_metadata;
        enrichWorkbookMetadata(path, workbook.data, zip_entries, rich_metadata);
        enrichStylesMetadata(path, rich_metadata);
        enrichTableMetadata(path, zip_entries, rich_metadata);

        const auto content = normalizeExtractedTextPreservingLines(text.str());
        if (content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_xlsx_text", "XLSX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content;
        doc.document_type = "xlsx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["xlsx_archive_backend"] = "libzip";
        doc.metadata["xlsx_xml_parser"] = "pugixml";
        doc.metadata["xlsx_sheet_count"] = std::to_string(sheet_count);
        doc.metadata["xlsx_row_count"] = std::to_string(row_count);
        doc.metadata["xlsx_cell_count"] = std::to_string(cell_count);
        doc.metadata["xlsx_sheet_names"] = joinFields(names, ",");
        doc.metadata["xlsx_formula_count"] = std::to_string(formula_count);
        doc.metadata["xlsx_merged_cell_count"] = std::to_string(merged_cell_count);
        doc.metadata["xlsx_styled_cell_count"] = std::to_string(styled_cell_count);
        doc.metadata["xlsx_unique_style_ref_count"] = std::to_string(style_refs.size());
        doc.metadata["xlsx_max_column_count"] = std::to_string(max_column_count);
        doc.metadata["xlsx_typed_cell_count_empty"] = std::to_string(type_counts[IngestedCellKind::Empty]);
        doc.metadata["xlsx_typed_cell_count_text"] = std::to_string(type_counts[IngestedCellKind::Text]);
        doc.metadata["xlsx_typed_cell_count_integer"] = std::to_string(type_counts[IngestedCellKind::Integer]);
        doc.metadata["xlsx_typed_cell_count_number"] = std::to_string(type_counts[IngestedCellKind::Number]);
        doc.metadata["xlsx_typed_cell_count_boolean"] = std::to_string(type_counts[IngestedCellKind::Boolean]);
        doc.metadata["xlsx_typed_cell_count_date"] = std::to_string(type_counts[IngestedCellKind::Date]);
        doc.metadata["xlsx_typed_cell_count_formula"] = std::to_string(type_counts[IngestedCellKind::Formula]);
        setMetadataIfNotEmpty(doc.metadata, "xlsx_formula_cells", joinValues(formula_cells, "; ", 32, 2048), 2048);
        setMetadataIfNotEmpty(doc.metadata, "xlsx_merged_ranges", joinValues(merged_ranges, ", ", 32, 2048), 2048);
        setMetadataIfNotEmpty(doc.metadata, "xlsx_dimension_ranges", joinValues(dimensions, ", ", 32, 1024), 1024);
        doc.metadata["structure_contract"] = "spreadsheet_rows_v2";
        doc.metadata["table_sheet_count"] = std::to_string(sheet_count);
        doc.metadata["table_row_count"] = std::to_string(row_count);
        doc.metadata["table_column_count"] = std::to_string(max_column_count);
        doc.metadata.insert(rich_metadata.begin(), rich_metadata.end());
        return doc;
#else
        (void)detected;
        result.skipped++;
        result.issues.push_back({
            IngestionIssueSeverity::WARNING,
            path,
            "xlsx_parser_unavailable",
            "XLSX ingestion requires libzip and pugixml development libraries"
        });
        return std::nullopt;
#endif
    }

private:
#if QORNIX_HAS_XLSX
    static IngestedCellKind inferXlsxCellKind(pugi::xml_node cell,
                                              const std::string& value,
                                              const std::string& formula) {
        if (!formula.empty()) {
            return IngestedCellKind::Formula;
        }
        const std::string type = cell.attribute("t").as_string();
        if (type == "b") {
            return IngestedCellKind::Boolean;
        }
        if (type == "s" || type == "str" || type == "inlineStr") {
            return value.empty() ? IngestedCellKind::Empty : IngestedCellKind::Text;
        }
        return inferCellKind(value);
    }

    static void enrichStylesMetadata(const std::string& path,
                                     std::map<std::string, std::string>& metadata) {
        const auto styles = readZipEntry(path, "xl/styles.xml", "xlsx_styles");
        if (!styles.ok) {
            metadata["xlsx_styles_available"] = "false";
            metadata["xlsx_cell_style_count"] = "0";
            metadata["xlsx_num_format_count"] = "0";
            return;
        }
        pugi::xml_document doc;
        if (!doc.load_string(styles.data.c_str())) {
            metadata["xlsx_styles_available"] = "false";
            metadata["xlsx_cell_style_count"] = "0";
            metadata["xlsx_num_format_count"] = "0";
            return;
        }
        metadata["xlsx_styles_available"] = "true";
        const auto cell_xfs = doc.child("styleSheet").child("cellXfs");
        const auto num_fmts = doc.child("styleSheet").child("numFmts");
        metadata["xlsx_cell_style_count"] = std::to_string(cell_xfs.attribute("count").as_uint(0));
        metadata["xlsx_num_format_count"] = std::to_string(num_fmts.attribute("count").as_uint(0));
    }

    static void enrichWorkbookMetadata(const std::string& path,
                                       const std::string& workbook_xml,
                                       const std::vector<std::string>& entries,
                                       std::map<std::string, std::string>& metadata) {
        (void)path;
        metadata["xlsx_archive_entry_count"] = std::to_string(entries.size());
        pugi::xml_document doc;
        if (!doc.load_string(workbook_xml.c_str())) {
            metadata["xlsx_workbook_metadata_available"] = "false";
            return;
        }
        metadata["xlsx_workbook_metadata_available"] = "true";
        std::vector<std::string> defined_names;
        for (auto defined_name : doc.child("workbook").child("definedNames").children("definedName")) {
            const std::string name = defined_name.attribute("name").as_string();
            if (!name.empty()) {
                defined_names.push_back(name + "=" + collectNodeText(defined_name));
            }
        }
        metadata["xlsx_defined_name_count"] = std::to_string(defined_names.size());
        setMetadataIfNotEmpty(metadata, "xlsx_defined_names", joinValues(defined_names, "; ", 24, 1024), 1024);
        const auto calc = doc.child("workbook").child("calcPr");
        if (calc) {
            setMetadataIfNotEmpty(metadata, "xlsx_calc_mode", calc.attribute("calcMode").as_string());
            setMetadataIfNotEmpty(metadata, "xlsx_calc_id", calc.attribute("calcId").as_string());
        }
    }

    static void enrichTableMetadata(const std::string& path,
                                    const std::vector<std::string>& entries,
                                    std::map<std::string, std::string>& metadata) {
        const auto table_entries = filterZipEntries(entries, "xl/tables/", ".xml");
        std::vector<std::string> table_names;
        size_t table_column_count = 0;
        for (const auto& entry : table_entries) {
            const auto xml = readZipEntry(path, entry, "xlsx_table");
            if (!xml.ok) {
                continue;
            }
            pugi::xml_document doc;
            if (!doc.load_string(xml.data.c_str())) {
                continue;
            }
            auto table = doc.child("table");
            std::string name = table.attribute("displayName").as_string();
            if (name.empty()) {
                name = table.attribute("name").as_string();
            }
            if (!name.empty()) {
                table_names.push_back(name);
            }
            table_column_count += table.child("tableColumns").attribute("count").as_uint(0);
        }
        metadata["xlsx_table_count"] = std::to_string(table_entries.size());
        metadata["xlsx_table_column_count"] = std::to_string(table_column_count);
        setMetadataIfNotEmpty(metadata, "xlsx_table_names", joinValues(table_names, ", ", 32, 1024), 1024);
    }
#endif
};

class PptxParser final : public DocumentParser {
public:
    std::string id() const override { return "pptx_libzip_pugixml"; }

    bool supports(const DetectedDocumentType& detected) const override {
        return detected.supported && detected.extension == ".pptx";
    }

    std::optional<IngestedDocument> parse(const std::string& path,
                                          const DetectedDocumentType& detected,
                                          const std::string& raw_content,
                                          IngestionJobResult& result) const override {
        if (raw_content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_file", "File is empty"});
            return std::nullopt;
        }

#if QORNIX_HAS_OPENXML
        const auto presentation = readZipEntry(path, "ppt/presentation.xml", "pptx_presentation");
        if (!presentation.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, presentation.code, presentation.message});
            return std::nullopt;
        }

        const auto rels = readZipEntry(path, "ppt/_rels/presentation.xml.rels", "pptx_presentation_rels");
        if (!rels.ok) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, rels.code, rels.message});
            return std::nullopt;
        }

        const auto slide_paths = parseSlidePaths(presentation.data, rels.data);
        std::stringstream text;
        size_t slide_count = 0;
        size_t text_run_count = 0;
        size_t notes_slide_count = 0;
        size_t notes_text_run_count = 0;
        size_t comments_count = 0;
        size_t alt_text_count = 0;
        size_t media_count = 0;
        std::set<std::string> slide_layout_paths;
        std::vector<std::string> alt_texts;
        std::vector<std::string> media_targets;
        std::vector<std::string> layout_names;
        std::vector<std::string> note_samples;
        std::vector<std::string> comment_samples;

        for (const auto& slide_path : slide_paths) {
            const auto slide = readZipEntry(path, slide_path, "pptx_slide");
            if (!slide.ok) {
                continue;
            }

            pugi::xml_document slide_doc;
            if (!slide_doc.load_string(slide.data.c_str())) {
                continue;
            }

            std::vector<std::string> values;
            collectPresentationText(slide_doc, values);
            std::vector<std::string> non_empty_values;
            for (const auto& value : values) {
                if (!value.empty()) {
                    non_empty_values.push_back(value);
                }
            }

            ++slide_count;
            if (!non_empty_values.empty()) {
                text_run_count += non_empty_values.size();
                text << "Slide " << slide_count << ": " << joinFields(non_empty_values, " | ") << "\n";
            } else {
                text << "Slide " << slide_count << ": [no visible text]\n";
            }

            const auto slide_alt_texts = collectAltText(slide_doc);
            if (!slide_alt_texts.empty()) {
                alt_text_count += slide_alt_texts.size();
                alt_texts.insert(alt_texts.end(), slide_alt_texts.begin(), slide_alt_texts.end());
                text << "Slide " << slide_count << " alt text: " << joinValues(slide_alt_texts, " | ", 16, 1024) << "\n";
            }

            const auto slide_rels_entry = readZipEntry(path, relationshipPartPath(slide_path), "pptx_slide_rels");
            if (!slide_rels_entry.ok) {
                continue;
            }
            const auto slide_rels = parseOpenXmlRelationships(slide_rels_entry.data, slide_path);
            for (const auto& relationship : slide_rels) {
                const auto type_lower = toLower(relationship.type);
                if (type_lower.find("notesslide") != std::string::npos) {
                    std::vector<std::string> notes;
                    const auto count = collectPartText(path, relationship.target, "pptx_notes", notes);
                    if (count > 0) {
                        ++notes_slide_count;
                        notes_text_run_count += count;
                        note_samples.insert(note_samples.end(), notes.begin(), notes.end());
                        text << "Slide " << slide_count << " notes: " << joinValues(notes, " | ", 24, 2048) << "\n";
                    }
                } else if (type_lower.find("comments") != std::string::npos || type_lower.find("comment") != std::string::npos) {
                    std::vector<std::string> comments;
                    const auto count = collectPartText(path, relationship.target, "pptx_comments", comments);
                    comments_count += count;
                    if (!comments.empty()) {
                        comment_samples.insert(comment_samples.end(), comments.begin(), comments.end());
                        text << "Slide " << slide_count << " comments: " << joinValues(comments, " | ", 24, 2048) << "\n";
                    }
                } else if (type_lower.find("slidelayout") != std::string::npos) {
                    slide_layout_paths.insert(relationship.target);
                    collectLayoutMetadata(path, relationship.target, layout_names);
                } else if (type_lower.find("image") != std::string::npos ||
                           type_lower.find("media") != std::string::npos ||
                           type_lower.find("video") != std::string::npos ||
                           type_lower.find("audio") != std::string::npos) {
                    ++media_count;
                    media_targets.push_back(relationship.target);
                }
            }
        }

        const auto content = normalizeExtractedTextPreservingLines(text.str());
        if (content.empty()) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::INFO, path, "empty_pptx_text", "PPTX produced no extractable text"});
            return std::nullopt;
        }

        IngestedDocument doc;
        doc.path = path;
        doc.content = content;
        doc.document_type = "pptx";
        doc.language = detected.language;
        doc.mime_type = detected.mime_type;
        doc.hash = hashContent(doc.content);
        doc.size_bytes = raw_content.size();
        doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
        doc.last_modified = fileTimeToSystem(fs::last_write_time(path));
        doc.metadata["mime_type"] = detected.mime_type;
        doc.metadata["ingestion_parser"] = id();
        doc.metadata["source_extension"] = detected.extension;
        doc.metadata["metadata_contract"] = "advanced_ingestion_metadata_v1";
        doc.metadata["pptx_archive_backend"] = "libzip";
        doc.metadata["pptx_xml_parser"] = "pugixml";
        doc.metadata["pptx_slide_count"] = std::to_string(slide_count);
        doc.metadata["pptx_text_run_count"] = std::to_string(text_run_count);
        doc.metadata["pptx_notes_slide_count"] = std::to_string(notes_slide_count);
        doc.metadata["pptx_notes_text_run_count"] = std::to_string(notes_text_run_count);
        doc.metadata["pptx_comments_count"] = std::to_string(comments_count);
        doc.metadata["pptx_alt_text_count"] = std::to_string(alt_text_count);
        doc.metadata["pptx_media_count"] = std::to_string(media_count);
        doc.metadata["pptx_slide_layout_count"] = std::to_string(slide_layout_paths.size());
        std::vector<std::string> layout_paths(slide_layout_paths.begin(), slide_layout_paths.end());
        setMetadataIfNotEmpty(doc.metadata, "pptx_slide_layout_paths", joinValues(layout_paths, ", ", 32, 1024), 1024);
        setMetadataIfNotEmpty(doc.metadata, "pptx_slide_layout_names", joinValues(layout_names, ", ", 32, 1024), 1024);
        setMetadataIfNotEmpty(doc.metadata, "pptx_alt_texts", joinValues(alt_texts, " | ", 32, 2048), 2048);
        setMetadataIfNotEmpty(doc.metadata, "pptx_media_targets", joinValues(media_targets, ", ", 32, 1024), 1024);
        setMetadataIfNotEmpty(doc.metadata, "pptx_notes", joinValues(note_samples, " | ", 16, 2048), 2048);
        setMetadataIfNotEmpty(doc.metadata, "pptx_comments", joinValues(comment_samples, " | ", 16, 2048), 2048);
        enrichPresentationProperties(path, doc.metadata);
        return doc;
#else
        (void)detected;
        result.skipped++;
        result.issues.push_back({
            IngestionIssueSeverity::WARNING,
            path,
            "pptx_parser_unavailable",
            "PPTX ingestion requires libzip and pugixml development libraries"
        });
        return std::nullopt;
#endif
    }

private:
#if QORNIX_HAS_OPENXML
    static std::vector<std::string> collectAltText(pugi::xml_document& doc) {
        std::vector<std::string> values;
        for (auto node : descendantsByLocalName(doc, "cNvPr")) {
            const auto name = xmlAttributeValue(node, {"name"});
            const auto title = xmlAttributeValue(node, {"title"});
            const auto descr = xmlAttributeValue(node, {"descr"});
            if (!title.empty()) {
                values.push_back(title);
            }
            if (!descr.empty() && descr != name) {
                values.push_back(descr);
            }
        }
        return values;
    }

    static size_t collectPartText(const std::string& archive_path,
                                  const std::string& entry,
                                  const std::string& code_prefix,
                                  std::vector<std::string>& values) {
        const auto xml = readZipEntry(archive_path, entry, code_prefix);
        if (!xml.ok) {
            return 0;
        }
        pugi::xml_document doc;
        if (!doc.load_string(xml.data.c_str())) {
            return 0;
        }
        std::vector<std::string> collected;
        collectPresentationText(doc, collected);
        for (const auto& value : collected) {
            if (!value.empty()) {
                values.push_back(value);
            }
        }
        return values.size();
    }

    static void collectLayoutMetadata(const std::string& archive_path,
                                      const std::string& layout_path,
                                      std::vector<std::string>& layout_names) {
        const auto xml = readZipEntry(archive_path, layout_path, "pptx_layout");
        if (!xml.ok) {
            return;
        }
        pugi::xml_document doc;
        if (!doc.load_string(xml.data.c_str())) {
            return;
        }
        auto c_sld_nodes = descendantsByLocalName(doc, "cSld");
        for (auto node : c_sld_nodes) {
            const auto name = xmlAttributeValue(node, {"name"});
            if (!name.empty()) {
                layout_names.push_back(name);
            }
        }
    }

    static void enrichPresentationProperties(const std::string& archive_path,
                                             std::map<std::string, std::string>& metadata) {
        const auto core = readZipEntry(archive_path, "docProps/core.xml", "pptx_core_props");
        if (core.ok) {
            setMetadataIfNotEmpty(metadata, "pptx_title", coreProperty(core.data, "title"));
            setMetadataIfNotEmpty(metadata, "pptx_subject", coreProperty(core.data, "subject"));
            setMetadataIfNotEmpty(metadata, "pptx_speaker", coreProperty(core.data, "creator"));
            setMetadataIfNotEmpty(metadata, "pptx_last_modified_by", coreProperty(core.data, "lastModifiedBy"));
            setMetadataIfNotEmpty(metadata, "pptx_created", coreProperty(core.data, "created"));
            setMetadataIfNotEmpty(metadata, "pptx_modified", coreProperty(core.data, "modified"));
        }
        const auto app = readZipEntry(archive_path, "docProps/app.xml", "pptx_app_props");
        if (app.ok) {
            setMetadataIfNotEmpty(metadata, "pptx_application", coreProperty(app.data, "Application"));
            setMetadataIfNotEmpty(metadata, "pptx_company", coreProperty(app.data, "Company"));
            setMetadataIfNotEmpty(metadata, "pptx_presentation_format", coreProperty(app.data, "PresentationFormat"));
            setMetadataIfNotEmpty(metadata, "pptx_slides_property", coreProperty(app.data, "Slides"));
        }
    }
#endif
};


} // namespace

IngestionPipeline::IngestionPipeline(Config config)
    : config_(std::move(config)) {
    if (config_.include_extensions.empty()) {
        config_.include_extensions = defaultExtensions();
    }
    for (auto& extension : config_.include_extensions) {
        extension = toLower(extension);
    }
    if (config_.exclude_directories.empty()) {
        config_.exclude_directories = {
            "cmake-build", ".git", "build", "__pycache__",
            "node_modules", ".venv", "dist", "bin", "obj",
            "qornix_rag/models", "/models/"
        };
    }
    parsers_.push_back(std::make_shared<DocxParser>());
    parsers_.push_back(std::make_shared<XlsxParser>());
    parsers_.push_back(std::make_shared<PptxParser>());
    parsers_.push_back(std::make_shared<PdfParser>());
    parsers_.push_back(std::make_shared<ImageOcrParser>());
    parsers_.push_back(std::make_shared<HtmlParser>());
    parsers_.push_back(std::make_shared<CsvParser>());
    parsers_.push_back(std::make_shared<PlainTextParser>());
}

DetectedDocumentType IngestionPipeline::detectFileType(const std::string& path) const {
    DetectedDocumentType detected;
    detected.extension = toLower(fs::path(path).extension().string());

    const auto& mapping = textExtensionMap();
    auto it = mapping.find(detected.extension);
    if (it == mapping.end()) {
        detected.supported = false;
        detected.reason = "unsupported_extension";
        return detected;
    }

    detected.mime_type = it->second.first;
    detected.language = it->second.second;
    if (detected.mime_type == "text/html") {
        detected.document_type = "html";
    } else if (detected.mime_type == "application/pdf") {
        detected.document_type = "pdf";
        detected.binary = true;
    } else if (detected.extension == ".docx") {
        detected.document_type = "docx";
        detected.binary = true;
    } else if (detected.extension == ".xlsx") {
        detected.document_type = "xlsx";
        detected.binary = true;
    } else if (detected.extension == ".pptx") {
        detected.document_type = "pptx";
        detected.binary = true;
    } else if (detected.mime_type.rfind("image/", 0) == 0) {
        detected.document_type = "image";
        detected.binary = true;
    } else if (detected.extension == ".csv") {
        detected.document_type = "csv";
    } else {
        detected.document_type = sourceExtensions().count(detected.extension) > 0 ? "source" : "config";
    }
    detected.supported = isAllowedExtension(detected.extension);
    detected.reason = detected.supported ? "supported_text" : "extension_not_enabled";
    return detected;
}

bool IngestionPipeline::shouldSkipDirectory(const std::string& path) const {
    for (const auto& skip : config_.exclude_directories) {
        if (!skip.empty() && path.find(skip) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string IngestionPipeline::determineRelativePath(const std::string& full_path) const {
    try {
        return fs::relative(full_path, config_.root_path).string();
    } catch (...) {
        return full_path;
    }
}

bool IngestionPipeline::isAllowedExtension(const std::string& extension) const {
    return std::find(config_.include_extensions.begin(),
                     config_.include_extensions.end(),
                     toLower(extension)) != config_.include_extensions.end();
}

const DocumentParser* IngestionPipeline::findParser(const DetectedDocumentType& detected) const {
    for (const auto& parser : parsers_) {
        if (parser && parser->supports(detected)) {
            return parser.get();
        }
    }
    return nullptr;
}

std::optional<IngestedDocument> IngestionPipeline::parseFile(
    const std::string& path,
    const DetectedDocumentType& detected,
    IngestionJobResult& result) const {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            result.errors++;
            result.issues.push_back({IngestionIssueSeverity::ERROR, path, "open_failed", "Could not open file"});
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        const auto* parser = findParser(detected);
        if (!parser) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "parser_not_found", "No parser registered for file type"});
            return std::nullopt;
        }

        auto parsed = parser->parse(path, detected, content, result);
        if (parsed && parsed->relative_path.empty()) {
            parsed->relative_path = determineRelativePath(path);
        }
        return parsed;
    } catch (const std::exception& e) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, path, "parse_failed", e.what()});
        return std::nullopt;
    }
}

IngestionJobResult IngestionPipeline::ingestPath(const std::string& path) const {
    IngestionJobResult result;
    result.files_seen = 1;

    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::WARNING, path, "not_regular_file", "Path is not a regular file"});
        return result;
    }

    const auto detected = detectFileType(path);
    if (!detected.supported) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::INFO, path, detected.reason, "Unsupported file type"});
        return result;
    }

    try {
        const auto max_bytes = config_.max_file_size_kb * 1024;
        if (fs::file_size(path) > max_bytes) {
            result.skipped++;
            result.issues.push_back({IngestionIssueSeverity::WARNING, path, "file_too_large", "File exceeds max size"});
            return result;
        }
    } catch (...) {
        result.skipped++;
        result.issues.push_back({IngestionIssueSeverity::WARNING, path, "file_size_failed", "Could not read file size"});
        return result;
    }

    auto parsed = parseFile(path, detected, result);
    if (parsed) {
        result.documents.push_back(std::move(*parsed));
        result.documents_imported++;
    }
    return result;
}

IngestionJobResult IngestionPipeline::ingestRoot() const {
    IngestionJobResult result;
    std::unordered_set<std::string> seen_hashes;

    std::error_code ec;
    if (!fs::exists(config_.root_path, ec)) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, config_.root_path, "root_missing", "Root path does not exist"});
        return result;
    }

    auto ingest_file = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file()) {
            return;
        }
        const auto path = entry.path().string();
        auto single = ingestPath(path);
        result.files_seen += single.files_seen;
        result.skipped += single.skipped;
        result.errors += single.errors;
        result.issues.insert(result.issues.end(), single.issues.begin(), single.issues.end());

        for (auto& doc : single.documents) {
            if (!seen_hashes.insert(doc.hash).second) {
                result.duplicates_found++;
                continue;
            }
            result.documents.push_back(std::move(doc));
            result.documents_imported++;
        }
    };

    try {
        if (config_.recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(config_.root_path)) {
                if (shouldSkipDirectory(entry.path().parent_path().string())) {
                    continue;
                }
                ingest_file(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(config_.root_path)) {
                ingest_file(entry);
            }
        }
    } catch (const std::exception& e) {
        result.errors++;
        result.issues.push_back({IngestionIssueSeverity::ERROR, config_.root_path, "scan_failed", e.what()});
    }

    return result;
}

std::string ingestionIssueSeverityToString(IngestionIssueSeverity severity) {
    switch (severity) {
        case IngestionIssueSeverity::INFO: return "info";
        case IngestionIssueSeverity::WARNING: return "warning";
        case IngestionIssueSeverity::ERROR: return "error";
    }
    return "unknown";
}

} // namespace qornix::rag
