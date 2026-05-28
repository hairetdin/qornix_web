/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "vector_store.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <boost/json.hpp>

#ifdef QORNIX_HAS_CURL
#include <curl/curl.h>
#endif

#ifdef QORNIX_HAS_LIBPQ
#include <libpq-fe.h>
#endif

#ifdef QORNIX_HAS_FAISS
#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>
#endif

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-pragmas"
    #pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
    #pragma clang diagnostic ignored "-Wmacro-redefined"
#endif

#include <hnswlib/hnswlib.h>

#ifdef __clang__
    #pragma clang diagnostic pop
#endif

struct LocalHnswVectorStore::Impl {
    std::unique_ptr<hnswlib::L2Space> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;
    size_t dimension = 0;
    size_t count = 0;
    std::string last_error;
};

namespace {

std::string lowerBackend(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

double cosineScoreFromDistance(float distance) {
    if (!std::isfinite(distance)) {
        return 0.0;
    }
    return 1.0 / (1.0 + static_cast<double>(std::max(0.0f, distance)));
}

bool isSafeSqlIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

VectorStoreDiagnostics makeDiagnostics(const VectorStore& store,
                                       std::string status,
                                       std::string detail = {}) {
    VectorStoreDiagnostics diagnostics;
    diagnostics.backend = store.backendName();
    diagnostics.status = std::move(status);
    diagnostics.detail = std::move(detail);
    diagnostics.ready = store.isReady();
    diagnostics.size = store.size();
    diagnostics.dimension = store.dimension();
    return diagnostics;
}

std::string vectorLiteral(const std::vector<float>& values) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
    return out.str();
}

#ifdef QORNIX_HAS_CURL
size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, size * nmemb);
    return size * nmemb;
}

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;
};

HttpResponse httpJsonRequest(const std::string& method,
                             const std::string& url,
                             const std::string& body,
                             const std::string& api_key) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl init failed";
        return response;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key.empty()) {
        headers = curl_slist_append(headers, ("api-key: " + api_key).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}
#endif

#ifdef QORNIX_HAS_FAISS
std::string labelsPath(const std::string& index_path) {
    return index_path + ".labels";
}
#endif

} // namespace

LocalHnswVectorStore::LocalHnswVectorStore()
    : impl_(std::make_unique<Impl>()) {
}

LocalHnswVectorStore::~LocalHnswVectorStore() = default;

std::string LocalHnswVectorStore::backendName() const {
    return "local_hnsw";
}

bool LocalHnswVectorStore::build(const std::vector<VectorRecord>& records, size_t dimension) {
    clear();
    if (dimension == 0 || records.empty()) {
        return false;
    }

    try {
        impl_->space = std::make_unique<hnswlib::L2Space>(dimension);
        impl_->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            impl_->space.get(),
            records.size(),
            16,
            200
        );

        size_t added = 0;
        for (const auto& record : records) {
            if (record.embedding.size() != dimension) {
                continue;
            }
            impl_->index->addPoint(record.embedding.data(), static_cast<hnswlib::labeltype>(record.label));
            ++added;
        }

        if (added == 0) {
            clear();
            return false;
        }

        impl_->dimension = dimension;
        impl_->count = added;
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        std::cerr << "Error building local HNSW vector store: " << e.what() << std::endl;
        clear();
        return false;
    }
}

std::vector<VectorSearchHit> LocalHnswVectorStore::search(const std::vector<float>& query, size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (!isReady() || query.size() != impl_->dimension || top_k == 0) {
        return hits;
    }

    try {
        const size_t k = std::min(top_k, impl_->count);
        auto result = impl_->index->searchKnn(query.data(), k);
        hits.reserve(result.size());

        while (!result.empty()) {
            auto top = result.top();
            const auto label = static_cast<size_t>(top.second);
            const float distance = top.first;
            hits.push_back(VectorSearchHit{
                label,
                distance,
                1.0 / (1.0 + static_cast<double>(distance))
            });
            result.pop();
        }

        std::reverse(hits.begin(), hits.end());
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        std::cerr << "Local HNSW vector search error: " << e.what() << std::endl;
        hits.clear();
    }

    return hits;
}

bool LocalHnswVectorStore::save(const std::string& path) const {
    if (!isReady() || path.empty()) {
        return false;
    }

    try {
        const std::filesystem::path index_path(path);
        if (index_path.has_parent_path()) {
            std::filesystem::create_directories(index_path.parent_path());
        }
        impl_->index->saveIndex(path);
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        std::cerr << "Error saving local HNSW vector store: " << e.what() << std::endl;
        return false;
    }
}

bool LocalHnswVectorStore::load(const std::string& path, size_t dimension, size_t max_elements) {
    clear();
    if (path.empty() || dimension == 0 || !std::filesystem::exists(path)) {
        return false;
    }

    try {
        impl_->space = std::make_unique<hnswlib::L2Space>(dimension);
        impl_->index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            impl_->space.get(),
            path,
            false,
            max_elements
        );
        impl_->dimension = dimension;
        impl_->count = impl_->index->cur_element_count;
        return impl_->count > 0;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        std::cerr << "Error loading local HNSW vector store: " << e.what() << std::endl;
        clear();
        return false;
    }
}

void LocalHnswVectorStore::clear() {
    impl_->index.reset();
    impl_->space.reset();
    impl_->dimension = 0;
    impl_->count = 0;
}

bool LocalHnswVectorStore::isReady() const {
    return impl_->index != nullptr && impl_->space != nullptr && impl_->dimension > 0 && impl_->count > 0;
}

size_t LocalHnswVectorStore::size() const {
    return impl_->count;
}

size_t LocalHnswVectorStore::dimension() const {
    return impl_->dimension;
}

std::string LocalHnswVectorStore::lastError() const {
    return impl_->last_error;
}

VectorStoreDiagnostics LocalHnswVectorStore::diagnostics() const {
    if (isReady()) {
        return makeDiagnostics(*this, "ready");
    }
    return makeDiagnostics(*this, "not_ready", impl_->last_error.empty() ? "index is not built or loaded" : impl_->last_error);
}

struct FaissVectorStore::Impl {
#ifdef QORNIX_HAS_FAISS
    std::unique_ptr<faiss::IndexFlatL2> index;
#endif
    std::vector<size_t> labels;
    size_t dimension = 0;
    std::string last_error;
};

FaissVectorStore::FaissVectorStore(VectorStoreOptions)
    : impl_(std::make_unique<Impl>()) {
}

FaissVectorStore::~FaissVectorStore() = default;

std::string FaissVectorStore::backendName() const {
    return "faiss";
}

bool FaissVectorStore::build(const std::vector<VectorRecord>& records, size_t dimension) {
    clear();
    if (dimension == 0 || records.empty()) {
        impl_->last_error = "no vectors to build";
        return false;
    }
#ifdef QORNIX_HAS_FAISS
    try {
        impl_->index = std::make_unique<faiss::IndexFlatL2>(dimension);
        std::vector<float> contiguous;
        contiguous.reserve(records.size() * dimension);
        impl_->labels.reserve(records.size());
        for (const auto& record : records) {
            if (record.embedding.size() != dimension) {
                continue;
            }
            contiguous.insert(contiguous.end(), record.embedding.begin(), record.embedding.end());
            impl_->labels.push_back(record.label);
        }
        if (impl_->labels.empty()) {
            clear();
            impl_->last_error = "no vectors matched dimension";
            return false;
        }
        impl_->index->add(impl_->labels.size(), contiguous.data());
        impl_->dimension = dimension;
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        clear();
        return false;
    }
#else
    impl_->last_error = "Faiss backend was not compiled; install Faiss headers/library and rebuild with QORNIX_ENABLE_FAISS=ON";
    return false;
#endif
}

std::vector<VectorSearchHit> FaissVectorStore::search(const std::vector<float>& query, size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (!isReady() || query.size() != impl_->dimension || top_k == 0) {
        return hits;
    }
#ifdef QORNIX_HAS_FAISS
    try {
        const size_t k = std::min(top_k, impl_->labels.size());
        std::vector<faiss::idx_t> indices(k);
        std::vector<float> distances(k);
        impl_->index->search(1, query.data(), k, distances.data(), indices.data());
        for (size_t i = 0; i < k; ++i) {
            if (indices[i] < 0 || static_cast<size_t>(indices[i]) >= impl_->labels.size()) {
                continue;
            }
            const float distance = distances[i];
            hits.push_back({impl_->labels[static_cast<size_t>(indices[i])], distance, cosineScoreFromDistance(distance)});
        }
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        hits.clear();
    }
#endif
    return hits;
}

bool FaissVectorStore::save(const std::string& path) const {
    if (!isReady() || path.empty()) {
        return false;
    }
#ifdef QORNIX_HAS_FAISS
    try {
        const std::filesystem::path index_path(path);
        if (index_path.has_parent_path()) {
            std::filesystem::create_directories(index_path.parent_path());
        }
        faiss::write_index(impl_->index.get(), path.c_str());
        std::ofstream labels(labelsPath(path), std::ios::trunc);
        for (size_t label : impl_->labels) {
            labels << label << '\n';
        }
        return static_cast<bool>(labels);
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)path;
    impl_->last_error = "Faiss backend was not compiled";
    return false;
#endif
}

bool FaissVectorStore::load(const std::string& path, size_t dimension, size_t) {
    clear();
    if (path.empty() || dimension == 0) {
        impl_->last_error = "path and dimension are required";
        return false;
    }
#ifdef QORNIX_HAS_FAISS
    try {
        std::unique_ptr<faiss::Index> raw_index(faiss::read_index(path.c_str()));
        auto* flat_index = dynamic_cast<faiss::IndexFlatL2*>(raw_index.get());
        if (flat_index) {
            raw_index.release();
        }
        impl_->index.reset(flat_index);
        if (!impl_->index || static_cast<size_t>(impl_->index->d) != dimension) {
            clear();
            impl_->last_error = "Faiss index dimension mismatch";
            return false;
        }
        std::ifstream labels(labelsPath(path));
        size_t label = 0;
        while (labels >> label) {
            impl_->labels.push_back(label);
        }
        if (impl_->labels.size() != static_cast<size_t>(impl_->index->ntotal)) {
            clear();
            impl_->last_error = "Faiss label sidecar count mismatch";
            return false;
        }
        impl_->dimension = dimension;
        return !impl_->labels.empty();
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        clear();
        return false;
    }
#else
    (void)path;
    (void)dimension;
    impl_->last_error = "Faiss backend was not compiled";
    return false;
#endif
}

void FaissVectorStore::clear() {
#ifdef QORNIX_HAS_FAISS
    impl_->index.reset();
#endif
    impl_->labels.clear();
    impl_->dimension = 0;
}

bool FaissVectorStore::isReady() const {
#ifdef QORNIX_HAS_FAISS
    return impl_->index != nullptr && impl_->dimension > 0 && !impl_->labels.empty();
#else
    return false;
#endif
}

size_t FaissVectorStore::size() const {
    return impl_->labels.size();
}

size_t FaissVectorStore::dimension() const {
    return impl_->dimension;
}

std::string FaissVectorStore::lastError() const {
    return impl_->last_error;
}

VectorStoreDiagnostics FaissVectorStore::diagnostics() const {
#ifdef QORNIX_HAS_FAISS
    if (isReady()) {
        return makeDiagnostics(*this, "ready");
    }
    return makeDiagnostics(*this, "not_ready", impl_->last_error.empty() ? "index is not built or loaded" : impl_->last_error);
#else
    return makeDiagnostics(*this, "dependency_unavailable",
        "Faiss backend was not compiled; install Faiss headers/library and rebuild with QORNIX_ENABLE_FAISS=ON");
#endif
}

QdrantVectorStore::QdrantVectorStore(VectorStoreOptions options)
    : options_(std::move(options)) {
}

std::string QdrantVectorStore::backendName() const {
    return "qdrant";
}

bool QdrantVectorStore::build(const std::vector<VectorRecord>& records, size_t dimension) {
    clear();
    if (options_.endpoint.empty() || options_.collection.empty()) {
        last_error_ = "qdrant endpoint and collection are required";
        return false;
    }
    if (dimension == 0 || records.empty()) {
        last_error_ = "no vectors to build";
        return false;
    }
#ifdef QORNIX_HAS_CURL
    namespace json = boost::json;
    const std::string base = options_.endpoint.back() == '/'
        ? options_.endpoint.substr(0, options_.endpoint.size() - 1)
        : options_.endpoint;
    const std::string collection_url = base + "/collections/" + options_.collection;

    auto delete_response = httpJsonRequest("DELETE", collection_url, "", options_.api_key);
    if (!delete_response.error.empty()
        || (delete_response.status != 0 && delete_response.status != 404
            && (delete_response.status < 200 || delete_response.status >= 300))) {
        last_error_ = "qdrant collection delete failed: " + delete_response.error
            + " status=" + std::to_string(delete_response.status);
        return false;
    }

    json::object create;
    json::object vectors;
    vectors["size"] = dimension;
    vectors["distance"] = options_.distance.empty() ? "Cosine" : options_.distance;
    create["vectors"] = vectors;

    auto create_response = httpJsonRequest("PUT", collection_url, json::serialize(create), options_.api_key);
    if (!create_response.error.empty() || create_response.status < 200 || create_response.status >= 300) {
        last_error_ = "qdrant collection create failed: " + create_response.error + " status=" + std::to_string(create_response.status);
        return false;
    }

    const size_t batch_size = std::max<size_t>(1, options_.upsert_batch_size);
    json::array points;
    size_t point_count = 0;
    bool upsert_ok = true;
    auto flush_points = [&]() {
        if (points.empty() || !upsert_ok) {
            return;
        }
        json::object upsert;
        upsert["points"] = std::move(points);
        auto upsert_response = httpJsonRequest("PUT", collection_url + "/points?wait=true", json::serialize(upsert), options_.api_key);
        if (!upsert_response.error.empty() || upsert_response.status < 200 || upsert_response.status >= 300) {
            last_error_ = "qdrant upsert failed: " + upsert_response.error + " status=" + std::to_string(upsert_response.status);
            upsert_ok = false;
        }
        points = json::array();
    };

    for (const auto& record : records) {
        if (record.embedding.size() != dimension) {
            continue;
        }
        json::object point;
        point["id"] = static_cast<std::uint64_t>(record.label);
        json::array vector;
        for (float value : record.embedding) {
            vector.push_back(value);
        }
        point["vector"] = std::move(vector);
        points.push_back(std::move(point));
        ++point_count;
        if (points.size() >= batch_size) {
            flush_points();
        }
    }
    if (point_count == 0) {
        last_error_ = "no vectors matched dimension";
        return false;
    }
    flush_points();
    if (!upsert_ok) {
        return false;
    }

    dimension_ = dimension;
    count_ = point_count;
    ready_ = true;
    return true;
#else
    last_error_ = "Qdrant backend requires CURL support";
    return false;
#endif
}

std::vector<VectorSearchHit> QdrantVectorStore::search(const std::vector<float>& query, size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (!isReady() || query.size() != dimension_ || top_k == 0) {
        return hits;
    }
#ifdef QORNIX_HAS_CURL
    namespace json = boost::json;
    const std::string base = options_.endpoint.back() == '/'
        ? options_.endpoint.substr(0, options_.endpoint.size() - 1)
        : options_.endpoint;
    json::object request;
    json::array vector;
    for (float value : query) {
        vector.push_back(value);
    }
    request["vector"] = std::move(vector);
    request["limit"] = top_k;
    auto response = httpJsonRequest("POST",
                                    base + "/collections/" + options_.collection + "/points/search",
                                    json::serialize(request),
                                    options_.api_key);
    if (!response.error.empty() || response.status < 200 || response.status >= 300) {
        last_error_ = "qdrant search failed: " + response.error + " status=" + std::to_string(response.status);
        return hits;
    }
    try {
        auto parsed = json::parse(response.body);
        const auto& object = parsed.as_object();
        auto result_it = object.find("result");
        if (result_it == object.end() || !result_it->value().is_array()) {
            return hits;
        }
        for (const auto& item : result_it->value().as_array()) {
            const auto& hit = item.as_object();
            auto id_it = hit.find("id");
            auto score_it = hit.find("score");
            if (id_it == hit.end() || score_it == hit.end()) {
                continue;
            }
            const auto label = static_cast<size_t>(id_it->value().to_number<std::uint64_t>());
            const auto score = score_it->value().to_number<double>();
            hits.push_back({label, static_cast<float>(1.0 - score), score});
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
        hits.clear();
    }
#endif
    return hits;
}

bool QdrantVectorStore::save(const std::string&) const {
    return isReady();
}

bool QdrantVectorStore::load(const std::string&, size_t dimension, size_t max_elements) {
    if (options_.endpoint.empty() || options_.collection.empty() || dimension == 0) {
        last_error_ = "qdrant endpoint, collection, and dimension are required";
        return false;
    }
    dimension_ = dimension;
    count_ = max_elements;
    ready_ = true;
    return true;
}

void QdrantVectorStore::clear() {
    dimension_ = 0;
    count_ = 0;
    ready_ = false;
}

bool QdrantVectorStore::isReady() const {
    return ready_ && dimension_ > 0;
}

size_t QdrantVectorStore::size() const {
    return count_;
}

size_t QdrantVectorStore::dimension() const {
    return dimension_;
}

std::string QdrantVectorStore::lastError() const {
    return last_error_;
}

VectorStoreDiagnostics QdrantVectorStore::diagnostics() const {
    if (options_.endpoint.empty() || options_.collection.empty()) {
        return makeDiagnostics(*this, "config_error", "qdrant endpoint and collection are required");
    }
#ifndef QORNIX_HAS_CURL
    return makeDiagnostics(*this, "dependency_unavailable", "Qdrant backend requires CURL support");
#else
    if (isReady()) {
        return makeDiagnostics(*this, "ready");
    }
    return makeDiagnostics(*this, "not_ready", last_error_.empty() ? "collection is configured but not loaded or built" : last_error_);
#endif
}

PgVectorStore::PgVectorStore(VectorStoreOptions options)
    : options_(std::move(options)) {
}

std::string PgVectorStore::backendName() const {
    return "pgvector";
}

bool PgVectorStore::build(const std::vector<VectorRecord>& records, size_t dimension) {
    clear();
    if (options_.connection_string.empty() || !isSafeSqlIdentifier(options_.table)) {
        last_error_ = "pgvector connection_string and safe table name are required";
        return false;
    }
    if (dimension == 0 || records.empty()) {
        last_error_ = "no vectors to build";
        return false;
    }
#ifdef QORNIX_HAS_LIBPQ
    PGconn* conn = PQconnectdb(options_.connection_string.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        last_error_ = PQerrorMessage(conn);
        PQfinish(conn);
        return false;
    }

    const std::string table = options_.table;
    const std::vector<std::string> statements = {
        "BEGIN",
        "CREATE EXTENSION IF NOT EXISTS vector",
        "CREATE TABLE IF NOT EXISTS " + table + " (id BIGINT PRIMARY KEY, embedding vector(" + std::to_string(dimension) + "))",
        "TRUNCATE TABLE " + table
    };
    for (const auto& sql : statements) {
        PGresult* result = PQexec(conn, sql.c_str());
        const auto status = PQresultStatus(result);
        if (status != PGRES_COMMAND_OK) {
            last_error_ = PQerrorMessage(conn);
            PQclear(result);
            PQexec(conn, "ROLLBACK");
            PQfinish(conn);
            return false;
        }
        PQclear(result);
    }

    size_t inserted = 0;
    for (const auto& record : records) {
        if (record.embedding.size() != dimension) {
            continue;
        }
        const std::string sql = "INSERT INTO " + table + " (id, embedding) VALUES ($1, $2::vector)";
        const std::string id = std::to_string(record.label);
        const std::string vector = vectorLiteral(record.embedding);
        const char* params[2] = {id.c_str(), vector.c_str()};
        PGresult* result = PQexecParams(conn, sql.c_str(), 2, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            last_error_ = PQerrorMessage(conn);
            PQclear(result);
            PQexec(conn, "ROLLBACK");
            PQfinish(conn);
            return false;
        }
        PQclear(result);
        ++inserted;
    }

    PGresult* commit = PQexec(conn, "COMMIT");
    if (PQresultStatus(commit) != PGRES_COMMAND_OK) {
        last_error_ = PQerrorMessage(conn);
        PQclear(commit);
        PQexec(conn, "ROLLBACK");
        PQfinish(conn);
        return false;
    }
    PQclear(commit);
    PQfinish(conn);
    if (inserted == 0) {
        last_error_ = "no vectors matched dimension";
        return false;
    }
    dimension_ = dimension;
    count_ = inserted;
    ready_ = true;
    return true;
#else
    last_error_ = "pgvector backend requires libpq support";
    return false;
#endif
}

std::vector<VectorSearchHit> PgVectorStore::search(const std::vector<float>& query, size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (!isReady() || query.size() != dimension_ || top_k == 0) {
        return hits;
    }
#ifdef QORNIX_HAS_LIBPQ
    PGconn* conn = PQconnectdb(options_.connection_string.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        last_error_ = PQerrorMessage(conn);
        PQfinish(conn);
        return hits;
    }

    const std::string sql = "SELECT id, embedding <-> $1::vector AS distance FROM " + options_.table +
        " ORDER BY embedding <-> $1::vector LIMIT " + std::to_string(top_k);
    const std::string vector = vectorLiteral(query);
    const char* params[1] = {vector.c_str()};
    PGresult* result = PQexecParams(conn, sql.c_str(), 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        last_error_ = PQerrorMessage(conn);
        PQclear(result);
        PQfinish(conn);
        return hits;
    }
    const int rows = PQntuples(result);
    for (int i = 0; i < rows; ++i) {
        const auto label = static_cast<size_t>(std::stoull(PQgetvalue(result, i, 0)));
        const auto distance = std::stof(PQgetvalue(result, i, 1));
        hits.push_back({label, distance, cosineScoreFromDistance(distance)});
    }
    PQclear(result);
    PQfinish(conn);
#endif
    return hits;
}

bool PgVectorStore::save(const std::string&) const {
    return isReady();
}

bool PgVectorStore::load(const std::string&, size_t dimension, size_t max_elements) {
    if (options_.connection_string.empty() || !isSafeSqlIdentifier(options_.table) || dimension == 0) {
        last_error_ = "pgvector connection_string, safe table name, and dimension are required";
        return false;
    }
    dimension_ = dimension;
    count_ = max_elements;
    ready_ = true;
    return true;
}

void PgVectorStore::clear() {
    dimension_ = 0;
    count_ = 0;
    ready_ = false;
}

bool PgVectorStore::isReady() const {
    return ready_ && dimension_ > 0;
}

size_t PgVectorStore::size() const {
    return count_;
}

size_t PgVectorStore::dimension() const {
    return dimension_;
}

std::string PgVectorStore::lastError() const {
    return last_error_;
}

VectorStoreDiagnostics PgVectorStore::diagnostics() const {
    if (options_.connection_string.empty() || !isSafeSqlIdentifier(options_.table)) {
        return makeDiagnostics(*this, "config_error", "pgvector connection_string and safe table name are required");
    }
#ifndef QORNIX_HAS_LIBPQ
    return makeDiagnostics(*this, "dependency_unavailable", "pgvector backend requires libpq support");
#else
    if (isReady()) {
        return makeDiagnostics(*this, "ready");
    }
    return makeDiagnostics(*this, "not_ready", last_error_.empty() ? "table is configured but not loaded or built" : last_error_);
#endif
}

std::unique_ptr<VectorStore> createVectorStore(const VectorStoreOptions& options) {
    const auto backend = lowerBackend(options.backend);
    if (backend == "local_hnsw" || backend == "hnsw") {
        return std::make_unique<LocalHnswVectorStore>();
    }
    if (backend == "faiss") {
        return std::make_unique<FaissVectorStore>(options);
    }
    if (backend == "qdrant") {
        return std::make_unique<QdrantVectorStore>(options);
    }
    if (backend == "pgvector" || backend == "postgres" || backend == "postgresql") {
        return std::make_unique<PgVectorStore>(options);
    }
    return nullptr;
}
