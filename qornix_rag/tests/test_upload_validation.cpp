/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_upload.h"
#include "ingestion_pipeline.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using qornix::rag::RagUploadConfig;
using qornix::rag::RagUploadPart;
using qornix::rag::RagUploadService;
using qornix::rag::sanitizeUploadFilename;

int main() {
    RagUploadConfig config;
    config.uploads_dir = (fs::temp_directory_path() / "qornix_upload_test").string();
    config.allowed_extensions = {".txt", ".pdf"};
    config.allowed_mime_types = {"text/plain", "application/pdf"};
    config.max_file_size_kb = 1;
    config.max_files_per_request = 2;
    fs::remove_all(config.uploads_dir);

    RagUploadService service(config);

    auto ok = service.validateFile("notes.txt", "text/plain; charset=utf-8", 32);
    assert(ok.ok);
    assert(ok.extension == ".txt");
    assert(ok.normalized_mime == "text/plain");

    auto unsafe_name = sanitizeUploadFilename("../bad name<script>.txt");
    assert(unsafe_name.find('/') == std::string::npos);
    assert(unsafe_name.find('<') == std::string::npos);
    assert(unsafe_name.find("..") == std::string::npos);

    auto bad_ext = service.validateFile("shell.sh", "text/plain", 16);
    assert(!bad_ext.ok);
    assert(bad_ext.code == "extension_not_allowed");

    auto bad_mime = service.validateFile("notes.txt", "application/x-msdownload", 16);
    assert(!bad_mime.ok);
    assert(bad_mime.code == "mime_not_allowed");

    auto too_large = service.validateFile("notes.txt", "text/plain", 4096);
    assert(!too_large.ok);
    assert(too_large.code == "file_too_large");

    const std::string boundary = "qornix-boundary";
    const std::string body =
        "--qornix-boundary\r\n"
        "Content-Disposition: form-data; name=\"auto_ingest\"\r\n\r\n"
        "true\r\n"
        "--qornix-boundary\r\n"
        "Content-Disposition: form-data; name=\"files\"; filename=\"notes.txt\"\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "hello upload\n"
        "--qornix-boundary--\r\n";
    auto multipart = service.parseMultipart("multipart/form-data; boundary=" + boundary, body);
    assert(multipart.ok);
    assert(multipart.files.size() == 1);
    assert(multipart.fields["auto_ingest"] == "true");
    assert(multipart.files[0].filename == "notes.txt");
    assert(multipart.files[0].content == "hello upload");

    std::string batch_id;
    auto stored = service.storeFiles(multipart.files, &batch_id);
    assert(stored.size() == 1);
    assert(!batch_id.empty());
    assert(fs::exists(stored[0].path));
    assert(stored[0].relative_path.find("qornix_upload_test") != std::string::npos);

    qornix::rag::IngestionPipeline::Config ingestion_config;
    ingestion_config.root_path = config.uploads_dir;
    qornix::rag::IngestionPipeline pipeline(ingestion_config);
    auto ingestion = pipeline.ingestRoot();
    assert(ingestion.errors == 0);
    assert(ingestion.documents_imported == 1);
    assert(!ingestion.documents.empty());
    assert(ingestion.documents[0].content.find("hello upload") != std::string::npos);

    std::string removed_abs;
    std::string removed_rel;
    assert(service.removeStoredFile(stored[0].relative_path, &removed_abs, &removed_rel));
    assert(!fs::exists(removed_abs));

    fs::remove_all(config.uploads_dir);
    std::cout << "Upload validation tests passed" << std::endl;
    return 0;
}
