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
    assert(plain_chunks.front().metadata["chunk_token_budget"] == "20");

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
    bool saw_symbol_name = false;
    for (const auto& chunk : code_chunks) {
        if (chunk.metadata.find("chunk_symbol_name") != chunk.metadata.end() &&
            chunk.metadata.at("chunk_symbol_name") == "Worker") {
            saw_symbol_name = true;
            assert(chunk.metadata.at("chunk_symbol_kind") == "class");
        }
    }
    assert(saw_symbol_name);
    bool saw_symbol_scope = false;
    for (const auto& chunk : code_chunks) {
        if (chunk.metadata.find("chunk_symbol_scope") != chunk.metadata.end() &&
            chunk.metadata.at("chunk_symbol_scope") == "class:Worker") {
            saw_symbol_scope = true;
        }
    }
    assert(saw_symbol_scope);

    Document pdf;
    pdf.path = "/tmp/manual.pdf";
    pdf.relative_path = "manual.pdf";
    pdf.type = "pdf";
    pdf.language = "PDF";
    pdf.metadata["mime_type"] = "application/pdf";
    pdf.metadata["pdf_page_count"] = "2";
    pdf.content = "Page 1:\n" + repeated_words("alpha", 10) + "\f\nPage 2:\n" + repeated_words("beta", 10);

    auto pdf_chunks = chunker.chunkDocument(pdf);
    assert(pdf_chunks.size() >= 2);
    bool saw_page_1 = false;
    bool saw_page_2 = false;
    for (const auto& chunk : pdf_chunks) {
        assert(chunk.metadata.at("chunk_strategy") == "pdf_page");
        if (chunk.metadata.at("chunk_page") == "1") {
            saw_page_1 = true;
        }
        if (chunk.metadata.at("chunk_page") == "2") {
            saw_page_2 = true;
        }
    }
    assert(saw_page_1);
    assert(saw_page_2);

    Document table;
    table.path = "/tmp/table.csv";
    table.relative_path = "table.csv";
    table.type = "csv";
    table.language = "CSV";
    table.content = "CSV table: table.csv\nColumns: 2\nRow 1: name | value\nRow 2: alpha | 10\nRow 3: beta | 20\n";

    auto table_chunks = chunker.chunkDocument(table);
    assert(table_chunks.size() == 3);
    assert(table_chunks.front().metadata.at("chunk_strategy") == "spreadsheet_table");
    assert(table_chunks.front().metadata.at("chunk_sheet") == "CSV");
    assert(table_chunks.front().metadata.at("chunk_row_start") == "1");

    Document ocr;
    ocr.path = "/tmp/sign.pgm";
    ocr.relative_path = "sign.pgm";
    ocr.type = "image";
    ocr.language = "Image";
    ocr.metadata["ingestion_parser"] = "image_tesseract_ocr";
    ocr.metadata["ocr_region_0"] = "full_image";
    ocr.metadata["ocr_caption"] = "Scanned label";
    ocr.content = repeated_words("ocr", 12);

    auto ocr_chunks = chunker.chunkDocument(ocr);
    assert(!ocr_chunks.empty());
    assert(ocr_chunks.front().metadata.at("chunk_strategy") == "image_ocr_region");
    assert(ocr_chunks.front().metadata.at("chunk_ocr_region") == "full_image");
    assert(ocr_chunks.front().metadata.at("chunk_ocr_caption") == "Scanned label");


    Document docx;
    docx.path = "/tmp/report.docx";
    docx.relative_path = "report.docx";
    docx.type = "docx";
    docx.language = "DOCX";
    docx.metadata["structure_contract"] = "docx_blocks_v1";
    docx.content = "Heading: Overview\n" + repeated_words("overview", 12) +
                   "\nHeading: Details\n" + repeated_words("details", 12);

    auto docx_chunks = chunker.chunkDocument(docx);
    assert(docx_chunks.size() >= 2);
    bool saw_docx_heading = false;
    for (const auto& chunk : docx_chunks) {
        assert(chunk.metadata.at("chunk_strategy") == "docx_heading");
        if (chunk.metadata.find("chunk_heading") != chunk.metadata.end() &&
            chunk.metadata.at("chunk_heading") == "Details") {
            saw_docx_heading = true;
        }
    }
    assert(saw_docx_heading);

    Document pptx;
    pptx.path = "/tmp/deck.pptx";
    pptx.relative_path = "deck.pptx";
    pptx.type = "pptx";
    pptx.language = "PPTX";
    pptx.metadata["pptx_slide_count"] = "2";
    pptx.content = "Slide 1: intro agenda\nSlide 1 notes: speaker note\nSlide 2: roadmap implementation\n";

    auto pptx_chunks = chunker.chunkDocument(pptx);
    assert(pptx_chunks.size() >= 2);
    bool saw_slide_2 = false;
    for (const auto& chunk : pptx_chunks) {
        assert(chunk.metadata.at("chunk_strategy") == "pptx_slide");
        if (chunk.metadata.find("chunk_slide") != chunk.metadata.end() &&
            chunk.metadata.at("chunk_slide") == "2") {
            saw_slide_2 = true;
        }
    }
    assert(saw_slide_2);

    Document tokenizer_limited = plain;
    tokenizer_limited.metadata["tokenizer_max_tokens"] = "8";
    auto limited_chunks = chunker.chunkDocument(tokenizer_limited);
    assert(limited_chunks.size() > plain_chunks.size());
    assert(limited_chunks.front().metadata.at("chunk_token_budget") == "8");

    std::cout << "DocumentChunker tests passed\n";
    return 0;
}
