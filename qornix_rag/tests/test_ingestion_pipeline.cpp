/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "../ingestion_pipeline.h"

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <unistd.h>

#ifndef QORNIX_HAS_LIBZIP
#define QORNIX_HAS_LIBZIP 0
#endif

#ifndef QORNIX_HAS_XLSX
#define QORNIX_HAS_XLSX 0
#endif

#ifndef QORNIX_HAS_OPENXML
#define QORNIX_HAS_OPENXML 0
#endif

namespace fs = std::filesystem;

using qornix::rag::IngestionPipeline;

static bool pdftotext_available() {
    return std::system("command -v pdftotext >/dev/null 2>&1") == 0;
}

static bool tesseract_available() {
    return std::system("command -v tesseract >/dev/null 2>&1") == 0;
}

static bool zip_available() {
    return std::system("command -v zip >/dev/null 2>&1") == 0;
}

static bool docx_parser_available() {
    return QORNIX_HAS_LIBZIP && zip_available();
}

static bool xlsx_parser_available() {
    return QORNIX_HAS_XLSX && zip_available();
}

static bool pptx_parser_available() {
    return QORNIX_HAS_OPENXML && zip_available();
}

static void write_simple_pdf(const fs::path& path) {
    std::vector<std::string> objects;
    objects.push_back("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    objects.push_back("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    objects.push_back("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n");
    objects.push_back("4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");
    const std::string stream = "BT\n/F1 18 Tf\n72 720 Td\n(PDF Parser Smoke) Tj\n0 -24 Td\n(Standalone RAG ingestion) Tj\nET\n";
    objects.push_back("5 0 obj\n<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "endstream\nendobj\n");

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    offsets.reserve(objects.size());
    for (const auto& object : objects) {
        offsets.push_back(pdf.size());
        pdf += object;
    }
    const size_t xref_offset = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (const auto offset : offsets) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

    std::ofstream(path, std::ios::binary) << pdf;
}

static void write_simple_docx(const fs::path& path) {
    auto work = path.parent_path() / "docx_work";
    fs::remove_all(work);
    fs::create_directories(work / "_rels");
    fs::create_directories(work / "word");

    std::ofstream(work / "[Content_Types].xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        << R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        << R"(<Default Extension="xml" ContentType="application/xml"/>)"
        << R"(<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>)"
        << R"(</Types>)";
    std::ofstream(work / "_rels" / ".rels")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        << R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>)"
        << R"(</Relationships>)";
    std::ofstream(work / "word" / "document.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        << R"(<w:body><w:p><w:r><w:t>DOCX Parser Smoke</w:t></w:r></w:p>)"
        << R"(<w:p><w:r><w:t>Standalone RAG document ingestion</w:t></w:r></w:p></w:body></w:document>)";

    fs::remove(path);
    const std::string command = "cd " + work.string() + " && zip -qr " + path.string() + " .";
    const int zip_status = std::system(command.c_str());
    fs::remove_all(work);
    if (zip_status != 0) {
        throw std::runtime_error("Could not create DOCX fixture");
    }
}

static void write_simple_xlsx(const fs::path& path) {
    auto work = path.parent_path() / "xlsx_work";
    fs::remove_all(work);
    fs::create_directories(work / "_rels");
    fs::create_directories(work / "xl" / "_rels");
    fs::create_directories(work / "xl" / "worksheets");

    std::ofstream(work / "[Content_Types].xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        << R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        << R"(<Default Extension="xml" ContentType="application/xml"/>)"
        << R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        << R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        << R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)"
        << R"(</Types>)";
    std::ofstream(work / "_rels" / ".rels")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        << R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        << R"(</Relationships>)";
    std::ofstream(work / "xl" / "workbook.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        << R"(<sheets><sheet name="Products" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    std::ofstream(work / "xl" / "_rels" / "workbook.xml.rels")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        << R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        << R"(</Relationships>)";
    std::ofstream(work / "xl" / "sharedStrings.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="6" uniqueCount="6">)"
        << R"(<si><t>Name</t></si><si><t>Price</t></si><si><t>Widget</t></si>)"
        << R"(<si><t>12.50</t></si><si><t>Gadget</t></si><si><t>18.00</t></si></sst>)";
    std::ofstream(work / "xl" / "worksheets" / "sheet1.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        << R"(<sheetData>)"
        << R"(<row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row>)"
        << R"(<row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2" t="s"><v>3</v></c></row>)"
        << R"(<row r="3"><c r="A3" t="s"><v>4</v></c><c r="B3" t="s"><v>5</v></c></row>)"
        << R"(</sheetData></worksheet>)";

    fs::remove(path);
    const std::string command = "cd " + work.string() + " && zip -qr " + path.string() + " .";
    const int zip_status = std::system(command.c_str());
    fs::remove_all(work);
    if (zip_status != 0) {
        throw std::runtime_error("Could not create XLSX fixture");
    }
}

static void write_simple_pptx(const fs::path& path) {
    auto work = path.parent_path() / "pptx_work";
    fs::remove_all(work);
    fs::create_directories(work / "_rels");
    fs::create_directories(work / "ppt" / "_rels");
    fs::create_directories(work / "ppt" / "slides");

    std::ofstream(work / "[Content_Types].xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        << R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        << R"(<Default Extension="xml" ContentType="application/xml"/>)"
        << R"(<Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>)"
        << R"(<Override PartName="/ppt/slides/slide1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>)"
        << R"(<Override PartName="/ppt/slides/slide2.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>)"
        << R"(</Types>)";
    std::ofstream(work / "_rels" / ".rels")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        << R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="ppt/presentation.xml"/>)"
        << R"(</Relationships>)";
    std::ofstream(work / "ppt" / "presentation.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        << R"(<p:sldIdLst><p:sldId id="256" r:id="rId1"/><p:sldId id="257" r:id="rId2"/></p:sldIdLst></p:presentation>)";
    std::ofstream(work / "ppt" / "_rels" / "presentation.xml.rels")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        << R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide1.xml"/>)"
        << R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide2.xml"/>)"
        << R"(</Relationships>)";
    std::ofstream(work / "ppt" / "slides" / "slide1.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">)"
        << R"(<p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>PPTX Parser Smoke</a:t></a:r></a:p>)"
        << R"(<a:p><a:r><a:t>Standalone RAG presentation ingestion</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";
    std::ofstream(work / "ppt" / "slides" / "slide2.xml")
        << R"(<?xml version="1.0" encoding="UTF-8"?>)"
        << R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">)"
        << R"(<p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>Second slide content</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";

    fs::remove(path);
    const std::string command = "cd " + work.string() + " && zip -qr " + path.string() + " .";
    const int zip_status = std::system(command.c_str());
    fs::remove_all(work);
    if (zip_status != 0) {
        throw std::runtime_error("Could not create PPTX fixture");
    }
}

static void write_simple_pgm(const fs::path& path) {
    const std::vector<std::string> glyphs = {
        " ###  ###  ### ",
        "#   ##   ##   #",
        "#   ##      #  ",
        "#   ##     #   ",
        "#   ##    #    ",
        "#   ##   #     ",
        " ###  ###  #####"
    };
    const int scale = 12;
    const int padding = 24;
    const int width = static_cast<int>(glyphs.front().size()) * scale + padding * 2;
    const int height = static_cast<int>(glyphs.size()) * scale + padding * 2;

    std::ofstream out(path);
    out << "P2\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int glyph_y = (y - padding) / scale;
            const int glyph_x = (x - padding) / scale;
            const bool ink = glyph_y >= 0
                && glyph_y < static_cast<int>(glyphs.size())
                && glyph_x >= 0
                && glyph_x < static_cast<int>(glyphs[glyph_y].size())
                && glyphs[glyph_y][glyph_x] == '#';
            out << (ink ? 0 : 255) << ' ';
        }
        out << '\n';
    }
}

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
    std::ofstream(dir / "table.csv") << "name,description,score\nalpha,\"first line\nsecond line\",10\nbeta,\"quoted \"\"value\"\"\",20\n";
    write_simple_pgm(dir / "ocr_sample.pgm");
    write_simple_pdf(dir / "guide.pdf");
    if (zip_available()) {
        write_simple_docx(dir / "guide.docx");
        write_simple_xlsx(dir / "workbook.xlsx");
        write_simple_pptx(dir / "deck.pptx");
    } else {
        std::ofstream(dir / "guide.docx", std::ios::binary) << "not a real docx";
        std::ofstream(dir / "workbook.xlsx", std::ios::binary) << "not a real xlsx";
        std::ofstream(dir / "deck.pptx", std::ios::binary) << "not a real pptx";
    }
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

    auto pdf = pipeline.detectFileType((fixture / "guide.pdf").string());
    assert(pdf.supported);
    assert(pdf.mime_type == "application/pdf");
    assert(pdf.document_type == "pdf");
    assert(pdf.binary);

    auto docx = pipeline.detectFileType((fixture / "guide.docx").string());
    assert(docx.supported);
    assert(docx.mime_type == "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    assert(docx.document_type == "docx");
    assert(docx.binary);

    auto csv = pipeline.detectFileType((fixture / "table.csv").string());
    assert(csv.supported);
    assert(csv.mime_type == "text/csv");
    assert(csv.document_type == "csv");

    auto xlsx = pipeline.detectFileType((fixture / "workbook.xlsx").string());
    assert(xlsx.supported);
    assert(xlsx.mime_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    assert(xlsx.document_type == "xlsx");
    assert(xlsx.binary);

    auto pptx = pipeline.detectFileType((fixture / "deck.pptx").string());
    assert(pptx.supported);
    assert(pptx.mime_type == "application/vnd.openxmlformats-officedocument.presentationml.presentation");
    assert(pptx.document_type == "pptx");
    assert(pptx.binary);

    auto image = pipeline.detectFileType((fixture / "ocr_sample.pgm").string());
    assert(image.supported);
    assert(image.mime_type == "image/x-portable-graymap");
    assert(image.document_type == "image");
    assert(image.binary);

    auto result = pipeline.ingestRoot();
    assert(result.files_seen >= 3);
    const auto expected_without_optional_ocr = 4
           + (pdftotext_available() ? 1 : 0)
           + (docx_parser_available() ? 1 : 0)
           + (xlsx_parser_available() ? 1 : 0)
           + (pptx_parser_available() ? 1 : 0);
    assert(result.documents_imported >= expected_without_optional_ocr);
    assert(result.documents_imported <= expected_without_optional_ocr + (tesseract_available() ? 1 : 0));
    assert(result.skipped >= 1);
    assert(result.errors == 0);

    bool found_readme = false;
    bool found_cpp = false;
    bool found_html = false;
    bool found_pdf = false;
    bool found_docx = false;
    bool found_csv = false;
    bool found_xlsx = false;
    bool found_pptx = false;
    bool found_image = false;
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
        if (doc.relative_path == "guide.pdf") {
            found_pdf = true;
            assert(doc.document_type == "pdf");
            assert(doc.mime_type == "application/pdf");
            assert(doc.metadata.at("ingestion_parser") == "pdf_pdftotext");
            assert(doc.metadata.at("pdf_text_extractor") == "pdftotext");
            assert(doc.metadata.at("structure_contract") == "pdf_pages_v1");
            assert(doc.metadata.at("pdf_page_count") == "1");
            assert(doc.content.find("PDF Parser Smoke") != std::string::npos);
            assert(doc.content.find("Standalone RAG ingestion") != std::string::npos);
        }
        if (doc.relative_path == "guide.docx") {
            found_docx = true;
            assert(doc.document_type == "docx");
            assert(doc.mime_type == "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
            assert(doc.metadata.at("ingestion_parser") == "docx_libzip");
            assert(doc.metadata.at("docx_archive_backend") == "libzip");
            assert(doc.content.find("DOCX Parser Smoke") != std::string::npos);
            assert(doc.content.find("Standalone RAG document ingestion") != std::string::npos);
        }
        if (doc.relative_path == "table.csv") {
            found_csv = true;
            assert(doc.document_type == "csv");
            assert(doc.mime_type == "text/csv");
            assert(doc.metadata.at("ingestion_parser") == "csv");
            assert(doc.metadata.at("csv_delimiter") == ",");
            assert(doc.metadata.at("csv_row_count") == "3");
            assert(doc.metadata.at("csv_column_count") == "3");
            assert(doc.metadata.at("structure_contract") == "spreadsheet_rows_v1");
            assert(doc.content.find("first line second line") != std::string::npos);
            assert(doc.content.find("quoted \"value\"") != std::string::npos);
        }
        if (doc.relative_path == "workbook.xlsx") {
            found_xlsx = true;
            assert(doc.document_type == "xlsx");
            assert(doc.mime_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
            assert(doc.metadata.at("ingestion_parser") == "xlsx_libzip_pugixml");
            assert(doc.metadata.at("xlsx_archive_backend") == "libzip");
            assert(doc.metadata.at("xlsx_xml_parser") == "pugixml");
            assert(doc.metadata.at("xlsx_sheet_count") == "1");
            assert(doc.metadata.at("xlsx_row_count") == "3");
            assert(doc.metadata.at("xlsx_cell_count") == "6");
            assert(doc.metadata.at("xlsx_sheet_names") == "Products");
            assert(doc.metadata.at("structure_contract") == "spreadsheet_rows_v1");
            assert(doc.content.find("Widget") != std::string::npos);
            assert(doc.content.find("Gadget") != std::string::npos);
        }
        if (doc.relative_path == "deck.pptx") {
            found_pptx = true;
            assert(doc.document_type == "pptx");
            assert(doc.mime_type == "application/vnd.openxmlformats-officedocument.presentationml.presentation");
            assert(doc.metadata.at("ingestion_parser") == "pptx_libzip_pugixml");
            assert(doc.metadata.at("pptx_archive_backend") == "libzip");
            assert(doc.metadata.at("pptx_xml_parser") == "pugixml");
            assert(doc.metadata.at("pptx_slide_count") == "2");
            assert(doc.metadata.at("pptx_text_run_count") == "3");
            assert(doc.content.find("PPTX Parser Smoke") != std::string::npos);
            assert(doc.content.find("Standalone RAG presentation ingestion") != std::string::npos);
            assert(doc.content.find("Second slide content") != std::string::npos);
        }
        if (doc.relative_path == "ocr_sample.pgm") {
            found_image = true;
            assert(doc.document_type == "image");
            assert(doc.mime_type == "image/x-portable-graymap");
            assert(doc.metadata.at("ingestion_parser") == "image_tesseract_ocr");
            assert(doc.metadata.at("image_ocr_engine") == "tesseract");
            assert(doc.metadata.at("structure_contract") == "ocr_regions_v1");
            assert(doc.metadata.at("ocr_region_0") == "full_image");
            assert(!doc.content.empty());
        }
        assert(doc.hash.size() > 0);
        assert(doc.lines_count > 0);
    }
    assert(found_readme);
    assert(found_cpp);
    assert(found_html);
    assert(found_pdf == pdftotext_available());
    assert(found_docx == docx_parser_available());
    assert(found_csv);
    assert(found_xlsx == xlsx_parser_available());
    assert(found_pptx == pptx_parser_available());
    (void)found_image;

    fs::remove_all(fixture);
    std::cout << "IngestionPipeline tests passed\n";
    return 0;
}
