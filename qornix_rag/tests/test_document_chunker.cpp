/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "document_chunker.h"
#include "core.h"

#include <cassert>
#include <iostream>
#include <string>

static std::string repeated_words(const std::string& prefix, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += prefix + std::to_string(i) + " ";
    }
    return text;
}

int main() {
    DocumentChunker chunker({20, 5, 3});

    Document plain;
    plain.path = "/tmp/plain.txt";
    plain.relative_path = "plain.txt";
    plain.type = "config";
    plain.language = "text";
    plain.content = repeated_words("token", 55);

    auto plain_chunks = chunker.chunkDocument(plain);
    assert(plain_chunks.size() >= 3);
    assert(plain_chunks.front().metadata["chunk_of"] == "plain.txt");
    assert(plain_chunks.front().metadata["chunk_strategy"] == "plain_text_token");
    assert(plain_chunks.front().metadata["chunk_index"] == "0");
    assert(plain_chunks.front().relative_path == "plain.txt#chunk-0");

    Document markdown;
    markdown.path = "/tmp/guide.md";
    markdown.relative_path = "guide.md";
    markdown.type = "markdown";
    markdown.language = "text";
    markdown.content =
        "# Intro\n" + repeated_words("intro", 18) +
        "\n## Setup\n" + repeated_words("setup", 18) +
        "\n## Usage\n" + repeated_words("usage", 18);

    auto markdown_chunks = chunker.chunkDocument(markdown);
    assert(markdown_chunks.size() >= 3);
    bool saw_setup = false;
    for (const auto& chunk : markdown_chunks) {
        auto heading = chunk.metadata.find("chunk_heading");
        if (heading != chunk.metadata.end() && heading->second == "Setup") {
            saw_setup = true;
        }
    }
    assert(saw_setup);

    Document code;
    code.path = "/tmp/service.cpp";
    code.relative_path = "service.cpp";
    code.type = "source";
    code.language = "cpp";
    code.content =
        "int first_function() {\n  return 1;\n}\n\n"
        "class Worker {\npublic:\n  void run();\n};\n\n"
        "std::string second_function(const std::string& value) {\n  return value;\n}\n";

    auto code_chunks = chunker.chunkDocument(code);
    assert(!code_chunks.empty());
    assert(code_chunks.front().metadata["chunk_strategy"] == "code_symbol");
    bool saw_symbol = false;
    for (const auto& chunk : code_chunks) {
        if (chunk.metadata.find("chunk_symbol") != chunk.metadata.end()) {
            saw_symbol = true;
        }
    }
    assert(saw_symbol);

    std::cout << "DocumentChunker tests passed\n";
    return 0;
}
