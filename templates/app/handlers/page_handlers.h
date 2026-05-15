#pragma once

#include "app_paths.h"
#include "handler_base.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class AppPageHandler : public HandlerBase {
public:
    AppPageHandler(std::string templatePath, std::string fallbackHtml)
        : templatePath_(std::move(templatePath)), fallbackHtml_(std::move(fallbackHtml)) {}

protected:
    void handleGet(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        buildHtmlResponse(res, http::status::ok, loadFile(templatePath_, fallbackHtml_));
    }

private:
    std::string templatePath_;
    std::string fallbackHtml_;

    static std::string loadFile(const std::string& path, const std::string& fallback) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return fallback;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};



class MarkdownDocHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>& pathParams
    ) override {
        const auto it = pathParams.find("filename");
        if (it == pathParams.end() || !isAllowedMarkdownFile(it->second)) {
            buildHtmlResponse(res, http::status::not_found, renderPage("Markdown document not found", "The requested markdown document is not available."));
            return;
        }

        const std::string filename = it->second;
        const std::string markdown = loadFile(qornix_app_paths::docPath(filename).string(), "");
        if (markdown.empty()) {
            buildHtmlResponse(res, http::status::not_found, renderPage("Markdown document not found", "The requested markdown document is not available."));
            return;
        }

        buildHtmlResponse(res, http::status::ok, renderMarkdownPage(filename, markdown));
    }

private:
    static bool isAllowedMarkdownFile(const std::string& filename) {
        if (filename.empty() || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
            return false;
        }
        if (filename.size() < 4 || filename.substr(filename.size() - 3) != ".md") {
            return false;
        }
        return std::all_of(filename.begin(), filename.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
        });
    }

    static std::string loadFile(const std::string& path, const std::string& fallback) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return fallback;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    static std::string escapeHtml(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        for (char ch : input) {
            switch (ch) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break;
                default: out += ch; break;
            }
        }
        return out;
    }

    static std::string titleFromFilename(std::string filename) {
        if (filename.size() > 3 && filename.substr(filename.size() - 3) == ".md") {
            filename.resize(filename.size() - 3);
        }
        std::replace(filename.begin(), filename.end(), '_', ' ');
        std::replace(filename.begin(), filename.end(), '-', ' ');
        if (!filename.empty()) {
            filename[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(filename[0])));
        }
        return filename;
    }

    static std::string renderPage(const std::string& title, const std::string& body) {
        std::ostringstream html;
        html << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>)HTML"
             << escapeHtml(title)
             << R"HTML( · @PROJECT_NAME@</title><link rel="stylesheet" href="/static/app.css"></head><body><div class="qx-shell"><header class="qx-topbar"><a class="qx-brand" href="/"><span class="qx-brand-mark">Qx</span><span><strong>@PROJECT_NAME@</strong><small>Documentation</small></span></a><nav class="qx-nav"><a href="/">Home</a><a href="/project-structure">Project Structure</a><a href="/docs">Docs</a><a href="/health">Health</a></nav></header><article class="qx-card qx-markdown-doc"><h1>)HTML"
             << escapeHtml(title)
             << "</h1><p>" << escapeHtml(body)
             << R"HTML(</p><div class="qx-actions"><a class="qx-btn" href="/docs">Back to docs</a></div></article></div></body></html>)HTML";
        return html.str();
    }

    static std::string trim(const std::string& value) {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
        if (first == value.end()) {
            return "";
        }
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
        return std::string(first, last);
    }

    static bool startsWith(const std::string& value, const std::string& prefix) {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    static std::string escapeChar(char ch) {
        switch (ch) {
            case '&': return "&amp;";
            case '<': return "&lt;";
            case '>': return "&gt;";
            case '"': return "&quot;";
            case '\'': return "&#39;";
            default: return std::string(1, ch);
        }
    }

    static bool isSafeLinkTarget(const std::string& target) {
        return startsWith(target, "http://") ||
               startsWith(target, "https://") ||
               startsWith(target, "/") ||
               startsWith(target, "#");
    }

    static std::string renderInlineMarkdown(const std::string& input) {
        std::string out;
        for (std::size_t i = 0; i < input.size();) {
            if (input[i] == '`') {
                const std::size_t end = input.find('`', i + 1);
                if (end != std::string::npos) {
                    out += "<code>";
                    out += escapeHtml(input.substr(i + 1, end - i - 1));
                    out += "</code>";
                    i = end + 1;
                    continue;
                }
            }

            if (i + 1 < input.size() && input[i] == '*' && input[i + 1] == '*') {
                const std::size_t end = input.find("**", i + 2);
                if (end != std::string::npos) {
                    out += "<strong>";
                    out += escapeHtml(input.substr(i + 2, end - i - 2));
                    out += "</strong>";
                    i = end + 2;
                    continue;
                }
            }

            if (input[i] == '[') {
                const std::size_t labelEnd = input.find(']', i + 1);
                if (labelEnd != std::string::npos && labelEnd + 1 < input.size() && input[labelEnd + 1] == '(') {
                    const std::size_t targetEnd = input.find(')', labelEnd + 2);
                    if (targetEnd != std::string::npos) {
                        const std::string label = input.substr(i + 1, labelEnd - i - 1);
                        const std::string target = input.substr(labelEnd + 2, targetEnd - labelEnd - 2);
                        if (isSafeLinkTarget(target)) {
                            out += "<a href=\"";
                            out += escapeHtml(target);
                            out += "\">";
                            out += escapeHtml(label);
                            out += "</a>";
                            i = targetEnd + 1;
                            continue;
                        }
                    }
                }
            }

            out += escapeChar(input[i]);
            ++i;
        }
        return out;
    }

    static std::vector<std::string> splitLines(std::string text) {
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        std::vector<std::string> lines;
        std::string line;
        std::istringstream stream(text);
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    static std::vector<std::string> splitTableRow(std::string line) {
        line = trim(line);
        if (!line.empty() && line.front() == '|') {
            line.erase(line.begin());
        }
        if (!line.empty() && line.back() == '|') {
            line.pop_back();
        }

        std::vector<std::string> cells;
        std::string cell;
        std::istringstream stream(line);
        while (std::getline(stream, cell, '|')) {
            cells.push_back(trim(cell));
        }
        return cells;
    }

    static bool isTableSeparator(const std::string& line) {
        const std::string value = trim(line);
        if (value.empty() || value.find('|') == std::string::npos) {
            return false;
        }
        int dashCount = 0;
        for (char ch : value) {
            if (ch == '-') {
                ++dashCount;
                continue;
            }
            if (ch == '|' || ch == ':' || std::isspace(static_cast<unsigned char>(ch))) {
                continue;
            }
            return false;
        }
        return dashCount >= 3;
    }

    static bool looksLikeTableHeader(const std::vector<std::string>& lines, std::size_t index) {
        return index + 1 < lines.size() &&
               lines[index].find('|') != std::string::npos &&
               isTableSeparator(lines[index + 1]);
    }

    static void closeOpenList(std::ostringstream& html, bool& inUnorderedList, bool& inOrderedList) {
        if (inUnorderedList) {
            html << "</ul>\n";
            inUnorderedList = false;
        }
        if (inOrderedList) {
            html << "</ol>\n";
            inOrderedList = false;
        }
    }

    static std::string renderMarkdownToHtml(const std::string& markdown) {
        const std::vector<std::string> lines = splitLines(markdown);
        std::ostringstream html;
        bool inCodeBlock = false;
        bool inUnorderedList = false;
        bool inOrderedList = false;

        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string rawLine = lines[i];
            const std::string line = trim(rawLine);

            if (startsWith(line, "```")) {
                closeOpenList(html, inUnorderedList, inOrderedList);
                if (!inCodeBlock) {
                    inCodeBlock = true;
                    html << "<pre><code>";
                } else {
                    inCodeBlock = false;
                    html << "</code></pre>\n";
                }
                continue;
            }

            if (inCodeBlock) {
                html << escapeHtml(rawLine) << "\n";
                continue;
            }

            if (line.empty()) {
                closeOpenList(html, inUnorderedList, inOrderedList);
                continue;
            }

            if (looksLikeTableHeader(lines, i)) {
                closeOpenList(html, inUnorderedList, inOrderedList);
                const auto headers = splitTableRow(lines[i]);
                html << "<div class=\"qx-table-wrap\"><table><thead><tr>";
                for (const auto& header : headers) {
                    html << "<th>" << renderInlineMarkdown(header) << "</th>";
                }
                html << "</tr></thead><tbody>";
                i += 2;
                for (; i < lines.size(); ++i) {
                    if (trim(lines[i]).empty() || lines[i].find('|') == std::string::npos) {
                        --i;
                        break;
                    }
                    const auto cells = splitTableRow(lines[i]);
                    html << "<tr>";
                    for (const auto& cell : cells) {
                        html << "<td>" << renderInlineMarkdown(cell) << "</td>";
                    }
                    html << "</tr>";
                }
                html << "</tbody></table></div>\n";
                continue;
            }

            if (line == "---" || line == "***" || line == "___") {
                closeOpenList(html, inUnorderedList, inOrderedList);
                html << "<hr>\n";
                continue;
            }

            int headingLevel = 0;
            while (headingLevel < 6 && headingLevel < static_cast<int>(line.size()) && line[headingLevel] == '#') {
                ++headingLevel;
            }
            if (headingLevel > 0 && headingLevel < static_cast<int>(line.size()) && std::isspace(static_cast<unsigned char>(line[headingLevel]))) {
                closeOpenList(html, inUnorderedList, inOrderedList);
                const std::string text = trim(line.substr(static_cast<std::size_t>(headingLevel)));
                html << "<h" << headingLevel << ">" << renderInlineMarkdown(text) << "</h" << headingLevel << ">\n";
                continue;
            }

            if (startsWith(line, ">")) {
                closeOpenList(html, inUnorderedList, inOrderedList);
                html << "<blockquote>" << renderInlineMarkdown(trim(line.substr(1))) << "</blockquote>\n";
                continue;
            }

            if ((startsWith(line, "- ") || startsWith(line, "* "))) {
                if (inOrderedList) {
                    html << "</ol>\n";
                    inOrderedList = false;
                }
                if (!inUnorderedList) {
                    html << "<ul>\n";
                    inUnorderedList = true;
                }
                html << "<li>" << renderInlineMarkdown(trim(line.substr(2))) << "</li>\n";
                continue;
            }

            const auto dot = line.find('.');
            if (dot != std::string::npos && dot > 0 && dot <= 3) {
                bool numericPrefix = true;
                for (std::size_t n = 0; n < dot; ++n) {
                    if (!std::isdigit(static_cast<unsigned char>(line[n]))) {
                        numericPrefix = false;
                        break;
                    }
                }
                if (numericPrefix && dot + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[dot + 1]))) {
                    if (inUnorderedList) {
                        html << "</ul>\n";
                        inUnorderedList = false;
                    }
                    if (!inOrderedList) {
                        html << "<ol>\n";
                        inOrderedList = true;
                    }
                    html << "<li>" << renderInlineMarkdown(trim(line.substr(dot + 1))) << "</li>\n";
                    continue;
                }
            }

            closeOpenList(html, inUnorderedList, inOrderedList);
            html << "<p>" << renderInlineMarkdown(line) << "</p>\n";
        }

        closeOpenList(html, inUnorderedList, inOrderedList);
        if (inCodeBlock) {
            html << "</code></pre>\n";
        }
        return html.str();
    }

    static std::string renderMarkdownPage(const std::string& filename, const std::string& markdown) {
        const std::string title = titleFromFilename(filename);
        std::ostringstream html;
        html << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>)HTML"
             << escapeHtml(title)
             << R"HTML( · @PROJECT_NAME@</title><link rel="stylesheet" href="/static/app.css"></head><body><div class="qx-shell"><header class="qx-topbar"><a class="qx-brand" href="/"><span class="qx-brand-mark">Qx</span><span><strong>@PROJECT_NAME@</strong><small>Markdown documentation</small></span></a><nav class="qx-nav"><a href="/">Home</a><a href="/docs">Docs</a><a href="/docs/getting-started">Start</a><a href="/health">Health</a></nav></header><section class="qx-page-intro"><p class="qx-kicker">Bundled markdown preview</p><h1>)HTML"
             << escapeHtml(title)
             << R"HTML(</h1><p class="qx-lead">This bundled markdown file is rendered as a readable in-app preview, so users can read it without an external markdown viewer.</p></section><article class="qx-card qx-markdown-doc qx-markdown-preview">)HTML"
             << renderMarkdownToHtml(markdown)
             << R"HTML(</article><footer class="qx-footer"><span>)HTML"
             << escapeHtml(filename)
             << R"HTML(</span><a href="/docs">Back to docs</a></footer></div></body></html>)HTML";
        return html.str();
    }
};

inline std::shared_ptr<AppPageHandler> makeHomePageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("home.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Web</title></head><body><h1>Qornix Web</h1><p>Landing page template was not found.</p><p><a href="/docs">Open documentation</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<AppPageHandler> makeDocsPageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("docs.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Qornix Web Docs</title></head><body><h1>Qornix Web Docs</h1><p>Docs template was not found.</p><p><a href="/">Back to home</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<AppPageHandler> makeGettingStartedPageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("getting_started.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Getting Started</title></head><body><h1>Getting Started</h1><p>Getting started template was not found.</p><p><a href="/docs">Back to docs</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<AppPageHandler> makeRoutingDocsPageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("routing.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Routing and Handlers</title></head><body><h1>Routing and Handlers</h1><p>Routing docs template was not found.</p><p><a href="/docs">Back to docs</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<AppPageHandler> makeConfigurationDocsPageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("configuration.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Configuration</title></head><body><h1>Configuration</h1><p>Configuration docs template was not found.</p><p><a href="/docs">Back to docs</a></p></body></html>)HTML"
    );
}

inline std::shared_ptr<AppPageHandler> makeDeploymentDocsPageHandler() {
    return std::make_shared<AppPageHandler>(
        qornix_app_paths::templatePath("deployment.html").string(),
        R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Deployment</title></head><body><h1>Deployment</h1><p>Deployment docs template was not found.</p><p><a href="/docs">Back to docs</a></p></body></html>)HTML"
    );
}


inline std::shared_ptr<MarkdownDocHandler> makeMarkdownDocHandler() {
    return std::make_shared<MarkdownDocHandler>();
}
