/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "sqlite_source.h"
#include "vector_store.h"

#include <boost/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " --sqlite-db <path> --source-id <id> --target <backend> [options]\n\n"
        << "Targets:\n"
        << "  local_hnsw  --index-path <path>\n"
        << "  faiss       --index-path <path>\n"
        << "  qdrant      --endpoint <url> --collection <name>\n"
        << "  pgvector    --connection-string <dsn> --table <name>\n\n"
        << "Options:\n"
        << "  --model-id <id>          Export only one embedding model id\n"
        << "  --label-map <path>       Write JSON label-to-chunk mapping\n"
        << "  --batch-size <n>         Backend upsert batch size (default: 512)\n"
        << "  --page-size <n>          SQLite export page size (default: 1000)\n"
        << "  --distance <name>        Qdrant distance (default: Cosine)\n"
        << "  --api-key <key>          Qdrant API key\n"
        << "  --recreate              Recreate/synchronize target backend\n";
}

std::map<std::string, std::string> parseArgs(int argc, char* argv[]) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--help" || key == "-h") {
            args["help"] = "true";
            continue;
        }
        if (key == "--recreate") {
            args["recreate"] = "true";
            continue;
        }
        if (key.rfind("--", 0) != 0 || i + 1 >= argc) {
            args["error"] = "invalid argument: " + key;
            return args;
        }
        key = key.substr(2);
        args[key] = argv[++i];
    }
    return args;
}

std::string arg(const std::map<std::string, std::string>& args,
                const std::string& key,
                const std::string& fallback = "") {
    auto it = args.find(key);
    return it == args.end() ? fallback : it->second;
}

size_t argSize(const std::map<std::string, std::string>& args,
               const std::string& key,
               size_t fallback) {
    try {
        return static_cast<size_t>(std::stoull(arg(args, key, std::to_string(fallback))));
    } catch (...) {
        return fallback;
    }
}

bool require(const std::map<std::string, std::string>& args,
             const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        if (arg(args, key).empty()) {
            std::cerr << "Missing required option --" << key << "\n";
            return false;
        }
    }
    return true;
}

bool writeLabelMap(const std::string& path,
                   const std::vector<PersistedEmbeddingRecord>& embeddings) {
    if (path.empty()) {
        return true;
    }
    boost::json::array items;
    for (const auto& embedding : embeddings) {
        boost::json::object item;
        item["label"] = static_cast<std::uint64_t>(embedding.vector.label);
        item["chunk_id"] = embedding.chunk_id;
        item["source_id"] = embedding.source_id;
        item["model_id"] = embedding.model_id;
        item["content_hash"] = embedding.content_hash;
        items.push_back(std::move(item));
    }
    std::filesystem::path out(path);
    if (out.has_parent_path()) {
        std::filesystem::create_directories(out.parent_path());
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    file << boost::json::serialize(items) << '\n';
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char* argv[]) {
    auto args = parseArgs(argc, argv);
    if (args.count("help")) {
        usage(argv[0]);
        return 0;
    }
    if (args.count("error")) {
        std::cerr << args["error"] << "\n";
        usage(argv[0]);
        return 2;
    }
    if (!require(args, {"sqlite-db", "source-id", "target"})) {
        usage(argv[0]);
        return 2;
    }

    const std::string db_path = arg(args, "sqlite-db");
    if (!std::filesystem::exists(db_path)) {
        std::cerr << "SQLite database does not exist: " << db_path << "\n";
        return 2;
    }

    SQLiteSource::Config sqlite_config;
    sqlite_config.db_path = db_path;
    sqlite_config.source_id = arg(args, "source-id");
    sqlite_config.auto_migrate = false;
    SQLiteSource source(sqlite_config);
    if (!source.initialize()) {
        std::cerr << "Failed to open SQLite source: " << db_path << "\n";
        return 1;
    }

    const size_t page_size = std::max<size_t>(1, argSize(args, "page-size", 1000));
    const std::string model_id = arg(args, "model-id");
    std::vector<PersistedEmbeddingRecord> embeddings;
    for (size_t offset = 0;; offset += page_size) {
        auto page = source.listPersistedEmbeddings(sqlite_config.source_id, model_id, page_size, offset);
        if (page.empty()) {
            break;
        }
        embeddings.insert(embeddings.end(), page.begin(), page.end());
        if (page.size() < page_size) {
            break;
        }
    }

    if (embeddings.empty()) {
        std::cerr << "No persisted embeddings found for source_id=" << sqlite_config.source_id;
        if (!model_id.empty()) {
            std::cerr << " model_id=" << model_id;
        }
        std::cerr << "\n";
        return 1;
    }

    const size_t dimension = embeddings.front().vector.embedding.size();
    std::vector<VectorRecord> records;
    records.reserve(embeddings.size());
    for (const auto& embedding : embeddings) {
        if (embedding.vector.embedding.size() != dimension) {
            std::cerr << "Mixed embedding dimensions are not supported in one migration\n";
            return 1;
        }
        records.push_back(embedding.vector);
    }

    VectorStoreOptions options;
    options.backend = arg(args, "target");
    options.index_path = arg(args, "index-path");
    options.endpoint = arg(args, "endpoint");
    options.api_key = arg(args, "api-key");
    options.collection = arg(args, "collection", options.collection);
    options.connection_string = arg(args, "connection-string");
    options.table = arg(args, "table", options.table);
    options.distance = arg(args, "distance", "Cosine");
    options.recreate = arg(args, "recreate") == "true";
    options.upsert_batch_size = std::max<size_t>(1, argSize(args, "batch-size", 512));

    const std::string backend = options.backend;
    if ((backend == "local_hnsw" || backend == "hnsw" || backend == "faiss") && options.index_path.empty()) {
        std::cerr << "--index-path is required for " << backend << "\n";
        return 2;
    }
    if (backend == "qdrant" && (options.endpoint.empty() || options.collection.empty())) {
        std::cerr << "--endpoint and --collection are required for qdrant\n";
        return 2;
    }
    if ((backend == "pgvector" || backend == "postgres" || backend == "postgresql")
        && (options.connection_string.empty() || options.table.empty())) {
        std::cerr << "--connection-string and --table are required for pgvector\n";
        return 2;
    }

    auto store = createVectorStore(options);
    if (!store) {
        std::cerr << "Unknown vector backend: " << backend << "\n";
        return 2;
    }
    if (!store->build(records, dimension)) {
        std::cerr << "Vector migration failed: " << store->lastError() << "\n";
        return 1;
    }
    if (!options.index_path.empty() && !store->save(options.index_path)) {
        std::cerr << "Vector index save failed: " << store->lastError() << "\n";
        return 1;
    }
    if (!writeLabelMap(arg(args, "label-map"), embeddings)) {
        std::cerr << "Failed to write label map\n";
        return 1;
    }

    const auto diagnostics = store->diagnostics();
    std::cout << "Migrated " << records.size() << " vectors"
              << " dimension=" << dimension
              << " backend=" << diagnostics.backend
              << " status=" << diagnostics.status << "\n";
    return 0;
}
