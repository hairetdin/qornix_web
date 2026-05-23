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
    };

    Config config_;

    std::vector<Section> splitMarkdownSections(const Document& document) const;
    std::vector<Section> splitCodeSections(const Document& document) const;
    std::vector<Document> chunkSections(const Document& document,
                                        const std::vector<Section>& sections,
                                        const std::string& strategy) const;
    std::vector<TokenSpan> tokenSpans(const std::string& text) const;
    std::string strategyFor(const Document& document) const;
    std::string makeChunkRelativePath(const Document& document, size_t chunk_index) const;
};
