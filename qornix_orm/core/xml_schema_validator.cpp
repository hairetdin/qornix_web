/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "xml_schema_validator.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>

#ifdef QORNIX_HAS_LIBXML2
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlstring.h>
#endif

std::string XmlValidationResult::summary() const {
    if (valid) {
        return "XML schema validation passed";
    }

    if (!schemaValidationAvailable) {
        return "XML schema validation is unavailable: libxml2 support is not enabled";
    }

    std::ostringstream out;
    out << "XML schema validation failed";
    if (!documentPath.empty()) {
        out << " for " << documentPath;
    }
    if (!errors.empty()) {
        out << ": " << errors.front().message;
    }
    return out.str();
}

XmlSchemaValidator::XmlSchemaValidator(std::string xsdPath)
    : xsdPath_(std::move(xsdPath)) {}

std::string XmlSchemaValidator::defaultSchemaPath() {
    namespace fs = std::filesystem;

    auto canonicalIfExists = [](const fs::path& candidate) -> std::string {
        std::error_code ec;
        if (!candidate.empty() && fs::exists(candidate, ec)) {
            auto canonical = fs::weakly_canonical(candidate, ec);
            return ec ? candidate.string() : canonical.string();
        }
        return {};
    };

    // Highest priority: explicit runtime override. This is useful for tests,
    // packaged applications and applications that run outside the source tree.
    if (const char* envPath = std::getenv("QORNIX_ORM_SCHEMA_APP_XSD")) {
        if (*envPath != '\0') {
            if (auto resolved = canonicalIfExists(envPath); !resolved.empty()) {
                return resolved;
            }
            return std::string(envPath);
        }
    }

#ifdef QORNIX_ORM_SCHEMA_APP_XSD_PATH
    // Build-system provided absolute path. This is the preferred default for
    // CMake targets because tests often run from cmake-build-* subdirectories.
    if (auto resolved = canonicalIfExists(QORNIX_ORM_SCHEMA_APP_XSD_PATH); !resolved.empty()) {
        return resolved;
    }
#endif

    const auto current = fs::current_path();

    // Common direct layouts.
    const std::vector<fs::path> candidates = {
        current / "qornix_orm" / "schema" / "schema_app.xsd",
        current / "schema" / "schema_app.xsd",
        current / ".." / "qornix_orm" / "schema" / "schema_app.xsd",
        current / ".." / "schema" / "schema_app.xsd",
        current / ".." / ".." / "qornix_orm" / "schema" / "schema_app.xsd",
        current / ".." / ".." / ".." / "qornix_orm" / "schema" / "schema_app.xsd"
    };

    for (const auto& candidate : candidates) {
        if (auto resolved = canonicalIfExists(candidate); !resolved.empty()) {
            return resolved;
        }
    }

    // Robust fallback: climb parent directories and look for the source-tree
    // layout. This covers paths like
    // build/example/dynamic_web_query_builder_server -> project root.
    for (fs::path dir = current; !dir.empty(); dir = dir.parent_path()) {
        for (const auto& relative : {
                 fs::path("qornix_orm/schema/schema_app.xsd"),
                 fs::path("schema/schema_app.xsd")
             }) {
            if (auto resolved = canonicalIfExists(dir / relative); !resolved.empty()) {
                return resolved;
            }
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    return "qornix_orm/schema/schema_app.xsd";
}

bool XmlSchemaValidator::isSchemaFormatVersionSupported(const std::string& version) {
    return version.empty() || version == "1.0";
}

#ifdef QORNIX_HAS_LIBXML2
namespace {

std::string trimErrorMessage(const char* message) {
    if (message == nullptr) {
        return {};
    }

    std::string value(message);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

void appendStructuredError(void* userData, xmlErrorPtr error) {
    if (userData == nullptr || error == nullptr) {
        return;
    }

    auto* errors = static_cast<std::vector<XmlValidationError>*>(userData);

    XmlValidationError validationError;
    validationError.line = error->line > 0 ? static_cast<std::size_t>(error->line) : 0;
    validationError.column = error->int2 > 0 ? static_cast<std::size_t>(error->int2) : 0;
    validationError.code = std::to_string(error->code);
    validationError.message = trimErrorMessage(error->message);

    if (error->node != nullptr) {
        auto* path = xmlGetNodePath(static_cast<xmlNodePtr>(error->node));
        if (path != nullptr) {
            validationError.elementPath = reinterpret_cast<const char*>(path);
            xmlFree(path);
        }
    }

    if (validationError.message.empty()) {
        validationError.message = "Unknown XML validation error";
    }

    errors->push_back(std::move(validationError));
}

void appendSimpleError(
    XmlValidationResult& result,
    const std::string& message,
    const std::string& code = "QORNIX_XML_VALIDATION_ERROR"
) {
    XmlValidationError error;
    error.code = code;
    error.message = message;
    result.errors.push_back(std::move(error));
}

std::string rootAttribute(xmlDocPtr doc, const char* attributeName) {
    if (doc == nullptr) {
        return {};
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root == nullptr) {
        return {};
    }

    xmlChar* value = xmlGetProp(root, reinterpret_cast<const xmlChar*>(attributeName));
    if (value == nullptr) {
        return {};
    }

    std::string result(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return result;
}

bool rootElementIsApplication(xmlDocPtr doc) {
    if (doc == nullptr) {
        return false;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root == nullptr || root->name == nullptr) {
        return false;
    }

    return xmlStrcmp(root->name, reinterpret_cast<const xmlChar*>("Application")) == 0;
}

XmlValidationResult validateDocWithSchema(
    xmlDocPtr doc,
    const std::string& documentPath,
    const std::string& xsdPath
) {
    XmlValidationResult result;
    result.valid = false;
    result.schemaPath = xsdPath;
    result.documentPath = documentPath;

    if (doc == nullptr) {
        appendSimpleError(result, "XML document is not well-formed", "QORNIX_XML_PARSE_ERROR");
        return result;
    }

    if (!rootElementIsApplication(doc)) {
        appendSimpleError(
            result,
            "Root element must be <Application>",
            "QORNIX_XML_ROOT_ELEMENT_ERROR"
        );
        return result;
    }

    const auto schemaFormatVersion = rootAttribute(doc, "schemaFormatVersion");
    if (!XmlSchemaValidator::isSchemaFormatVersionSupported(schemaFormatVersion)) {
        appendSimpleError(
            result,
            "Unsupported schemaFormatVersion: " + schemaFormatVersion,
            "QORNIX_SCHEMA_VERSION_UNSUPPORTED"
        );
        return result;
    }

    std::vector<XmlValidationError> parserErrors;
    xmlSchemaParserCtxtPtr parserCtxt = xmlSchemaNewParserCtxt(xsdPath.c_str());
    if (parserCtxt == nullptr) {
        appendSimpleError(
            result,
            "Cannot create XSD parser context for: " + xsdPath,
            "QORNIX_XSD_CONTEXT_ERROR"
        );
        return result;
    }

    xmlSchemaSetParserStructuredErrors(parserCtxt, appendStructuredError, &parserErrors);
    xmlSchemaPtr schema = xmlSchemaParse(parserCtxt);
    xmlSchemaFreeParserCtxt(parserCtxt);

    if (schema == nullptr) {
        result.errors = std::move(parserErrors);
        if (result.errors.empty()) {
            appendSimpleError(
                result,
                "Cannot parse XSD schema: " + xsdPath,
                "QORNIX_XSD_PARSE_ERROR"
            );
        }
        return result;
    }

    std::vector<XmlValidationError> validationErrors;
    xmlSchemaValidCtxtPtr validCtxt = xmlSchemaNewValidCtxt(schema);
    if (validCtxt == nullptr) {
        xmlSchemaFree(schema);
        appendSimpleError(
            result,
            "Cannot create XML schema validation context",
            "QORNIX_XSD_VALIDATION_CONTEXT_ERROR"
        );
        return result;
    }

    xmlSchemaSetValidStructuredErrors(validCtxt, appendStructuredError, &validationErrors);
    const int validationCode = xmlSchemaValidateDoc(validCtxt, doc);

    xmlSchemaFreeValidCtxt(validCtxt);
    xmlSchemaFree(schema);

    if (validationCode == 0) {
        result.valid = true;
        return result;
    }

    result.errors = std::move(validationErrors);
    if (result.errors.empty()) {
        appendSimpleError(
            result,
            "XML document does not conform to schema_app.xsd",
            "QORNIX_XSD_VALIDATION_FAILED"
        );
    }
    return result;
}

xmlDocPtr readXmlFileWithErrors(const std::string& path, std::vector<XmlValidationError>& errors) {
    xmlSetStructuredErrorFunc(&errors, appendStructuredError);
    xmlDocPtr doc = xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    xmlSetStructuredErrorFunc(nullptr, nullptr);
    return doc;
}

xmlDocPtr readXmlStringWithErrors(
    const std::string& xmlContent,
    const std::string& documentName,
    std::vector<XmlValidationError>& errors
) {
    xmlSetStructuredErrorFunc(&errors, appendStructuredError);
    xmlDocPtr doc = xmlReadMemory(
        xmlContent.c_str(),
        static_cast<int>(xmlContent.size()),
        documentName.c_str(),
        nullptr,
        XML_PARSE_NONET | XML_PARSE_NOBLANKS
    );
    xmlSetStructuredErrorFunc(nullptr, nullptr);
    return doc;
}

} // namespace
#endif

XmlValidationResult XmlSchemaValidator::validateFile(const std::string& xmlFilePath) const {
    XmlValidationResult result;
    result.schemaPath = xsdPath_;
    result.documentPath = xmlFilePath;

#ifdef QORNIX_HAS_LIBXML2
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(xsdPath_, ec)) {
        appendSimpleError(
            result,
            "XSD schema file not found: " + xsdPath_,
            "QORNIX_XSD_NOT_FOUND"
        );
        return result;
    }

    if (!fs::exists(xmlFilePath, ec)) {
        appendSimpleError(
            result,
            "XML file not found: " + xmlFilePath,
            "QORNIX_XML_NOT_FOUND"
        );
        return result;
    }

    std::vector<XmlValidationError> parseErrors;
    xmlDocPtr doc = readXmlFileWithErrors(xmlFilePath, parseErrors);
    if (doc == nullptr) {
        result.errors = std::move(parseErrors);
        if (result.errors.empty()) {
            appendSimpleError(result, "XML document is not well-formed", "QORNIX_XML_PARSE_ERROR");
        }
        return result;
    }

    result = validateDocWithSchema(doc, xmlFilePath, xsdPath_);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return result;
#else
    result.schemaValidationAvailable = false;
    result.valid = false;
    XmlValidationError error;
    error.code = "QORNIX_XSD_VALIDATION_UNAVAILABLE";
    error.message = "libxml2 support is not enabled; rebuild qornix_orm with libxml2 to validate schema_app.xsd";
    result.errors.push_back(std::move(error));
    return result;
#endif
}

XmlValidationResult XmlSchemaValidator::validateString(
    const std::string& xmlContent,
    const std::string& documentName
) const {
    XmlValidationResult result;
    result.schemaPath = xsdPath_;
    result.documentPath = documentName;

#ifdef QORNIX_HAS_LIBXML2
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(xsdPath_, ec)) {
        appendSimpleError(
            result,
            "XSD schema file not found: " + xsdPath_,
            "QORNIX_XSD_NOT_FOUND"
        );
        return result;
    }

    std::vector<XmlValidationError> parseErrors;
    xmlDocPtr doc = readXmlStringWithErrors(xmlContent, documentName, parseErrors);
    if (doc == nullptr) {
        result.errors = std::move(parseErrors);
        if (result.errors.empty()) {
            appendSimpleError(result, "XML document is not well-formed", "QORNIX_XML_PARSE_ERROR");
        }
        return result;
    }

    result = validateDocWithSchema(doc, documentName, xsdPath_);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return result;
#else
    result.schemaValidationAvailable = false;
    result.valid = false;
    XmlValidationError error;
    error.code = "QORNIX_XSD_VALIDATION_UNAVAILABLE";
    error.message = "libxml2 support is not enabled; rebuild qornix_orm with libxml2 to validate schema_app.xsd";
    result.errors.push_back(std::move(error));
    return result;
#endif
}
