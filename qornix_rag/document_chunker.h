/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct Document;

class DocumentChunker {
public:
    struct Config {
        size_t max_tokens = 220;
        size_t overlap_tokens = 40;
        size_t min_chunk_tokens = 24;
        size_t tokenizer_max_tokens = 0;
    };

    DocumentChunker();
    explicit DocumentChunker(Config config);

    std::vector<Document> chunkDocument(const Document& document) const;

private:
    struct TokenSpan {
        size_t start = 0;
        size_t end = 0;
    };

    struct Section {
        std::string text;
        size_t char_start = 0;
        std::string heading;
        std::string symbol;
        std::string symbol_name;
        std::string symbol_kind;
        std::string symbol_scope;
        std::string block_kind;
        size_t page_number = 0;
        size_t slide_number = 0;
        std::string sheet_name;
        size_t row_start = 0;
        size_t row_end = 0;
        std::string ocr_region;
        std::string ocr_caption;
    };

    Config config_;

    std::vector<Section> splitMarkdownSections(const Document& document) const;
    std::vector<Section> splitCodeSections(const Document& document) const;
    std::vector<Section> splitDocxSections(const Document& document) const;
    std::vector<Section> splitPdfPageSections(const Document& document) const;
    std::vector<Section> splitPptxSections(const Document& document) const;
    std::vector<Section> splitSpreadsheetSections(const Document& document) const;
    std::vector<Section> splitOcrSections(const Document& document) const;
    std::vector<Document> chunkSections(const Document& document,
                                        const std::vector<Section>& sections,
                                        const std::string& strategy) const;
    std::vector<TokenSpan> tokenSpans(const std::string& text) const;
    size_t effectiveMaxTokens(const Document& document) const;
    std::string strategyFor(const Document& document) const;
    std::string makeChunkRelativePath(const Document& document, size_t chunk_index) const;
};
