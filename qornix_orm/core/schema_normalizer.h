/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "schema_document.h"

/**
 * Controls how aggressive schema normalization should be.
 *
 * Sprint 26 intentionally keeps normalization conservative: the original
 * uploaded XML remains a separate artifact, while SchemaNormalizer returns a
 * normalized copy that can later be used by semantic diff.
 */
struct SchemaNormalizationOptions {
    bool normalizeIdentifierWhitespace = true;
    bool normalizeTypeAliases = true;
    bool normalizeDefaultValues = true;
    bool normalizeActionKeywords = true;
    bool sortComparableObjects = true;
};

struct SchemaNormalizationWarning {
    std::string code;
    std::string message;
    std::string elementPath;
};

struct SchemaNormalizationResult {
    SchemaDocument document;
    std::vector<SchemaNormalizationWarning> warnings;

    bool hasWarnings() const {
        return !warnings.empty();
    }
};

/**
 * Converts a SchemaDocument into a stable semantic form suitable for future
 * diff/plan/apply code.
 *
 * The normalizer does not overwrite the user uploaded XML and does not modify
 * the input document. It creates a normalized copy and records warnings for
 * ambiguous transformations.
 */
class SchemaNormalizer {
public:
    static SchemaNormalizationResult normalize(
        const SchemaDocument& document,
        const SchemaNormalizationOptions& options = SchemaNormalizationOptions{}
    );
};
