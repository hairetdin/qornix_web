/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "../ingestion_pipeline.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

using qornix::rag::IngestionPipeline;

static fs::path make_fixture() {
    auto dir = fs::temp_directory_path() / ("qornix_ingestion_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "src");
    fs::create_directories(dir / "build");

    std::ofstream(dir / "README.md") << "# Docs\nMarkdown content.\n";
    std::ofstream(dir / "src" / "main.cpp") << "int main() { return 0; }\n";
    std::ofstream(dir / "page.html")
        << "<!doctype html><html><head><title>HTML Guide &amp; Docs</title>"
        << "<style>.hidden{display:none}</style></head><body>"
        << "<h1>Welcome</h1><p>Use <strong>RAG</strong> routes.</p>"
        << "<script>ignored()</script></body></html>";
    std::ofstream(dir / "notes.bin", std::ios::binary) << std::string("abc\0def", 7);
    std::ofstream(dir / "build" / "ignored.md") << "# Ignored\n";
    return dir;
}

int main() {
    auto fixture = make_fixture();

    IngestionPipeline::Config config;
    config.root_path = fixture.string();
    config.max_file_size_kb = 64;
    IngestionPipeline pipeline(config);

    auto md = pipeline.detectFileType((fixture / "README.md").string());
    assert(md.supported);
    assert(md.mime_type == "text/markdown");
    assert(md.language == "Markdown");
    assert(md.document_type == "config");

    auto cpp = pipeline.detectFileType((fixture / "src" / "main.cpp").string());
    assert(cpp.supported);
    assert(cpp.document_type == "source");
    assert(cpp.language == "C++");

    auto unsupported = pipeline.detectFileType((fixture / "notes.bin").string());
    assert(!unsupported.supported);
    assert(unsupported.reason == "unsupported_extension");

    auto html = pipeline.detectFileType((fixture / "page.html").string());
    assert(html.supported);
    assert(html.mime_type == "text/html");
    assert(html.document_type == "html");

    auto result = pipeline.ingestRoot();
    assert(result.files_seen >= 3);
    assert(result.documents_imported == 3);
    assert(result.skipped >= 1);
    assert(result.errors == 0);

    bool found_readme = false;
    bool found_cpp = false;
    bool found_html = false;
    for (const auto& doc : result.documents) {
        if (doc.relative_path == "README.md") {
            found_readme = true;
            assert(doc.metadata.at("mime_type") == "text/markdown");
            assert(doc.metadata.at("ingestion_parser") == "plain_text");
        }
        if (doc.relative_path == "src/main.cpp") {
            found_cpp = true;
            assert(doc.document_type == "source");
        }
        if (doc.relative_path == "page.html") {
            found_html = true;
            assert(doc.document_type == "html");
            assert(doc.metadata.at("ingestion_parser") == "html");
            assert(doc.metadata.at("title") == "HTML Guide & Docs");
            assert(doc.content.find("<script>") == std::string::npos);
            assert(doc.content.find("ignored()") == std::string::npos);
            assert(doc.content.find("Welcome") != std::string::npos);
            assert(doc.content.find("RAG") != std::string::npos);
        }
        assert(doc.hash.size() > 0);
        assert(doc.lines_count > 0);
    }
    assert(found_readme);
    assert(found_cpp);
    assert(found_html);

    fs::remove_all(fixture);
    std::cout << "IngestionPipeline tests passed\n";
    return 0;
}
