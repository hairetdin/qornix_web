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

struct XmlValidationError {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string elementPath;
    std::string code;
    std::string message;
};

struct XmlValidationResult {
    bool valid = false;
    bool schemaValidationAvailable = true;
    std::string schemaPath;
    std::string documentPath;
    std::vector<XmlValidationError> errors;

    bool ok() const {
        return valid;
    }

    std::string summary() const;
};

class XmlSchemaValidator {
public:
    explicit XmlSchemaValidator(std::string xsdPath = defaultSchemaPath());

    static std::string defaultSchemaPath();
    static bool isSchemaFormatVersionSupported(const std::string& version);

    const std::string& schemaPath() const {
        return xsdPath_;
    }

    XmlValidationResult validateFile(const std::string& xmlFilePath) const;
    XmlValidationResult validateString(
        const std::string& xmlContent,
        const std::string& documentName = "<memory>"
    ) const;

private:
    std::string xsdPath_;
};
