#pragma once
// Plaintext ingestion: CSV → columnar plaintext → encrypted storage.

#include "proto1/fhe_runtime.h"
#include "proto1/storage.h"
#include "proto1/catalog.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace proto1 {

// A plaintext columnar batch (pre-encryption).
struct PlaintextBatch {
    std::string tableId;
    uint32_t    chunkId{0};
    int64_t     logicalRowStart{0};
    std::unordered_map<std::string, std::vector<int64_t>> columns;

    size_t rowCount() const {
        if (columns.empty()) return 0;
        return columns.begin()->second.size();
    }
};

// Read a CSV file into plaintext batches.
// The first row must be a header matching the schema column names.
std::vector<PlaintextBatch>
readCSV(const std::string& path,
        const TableSchema& schema,
        size_t chunkSize = 1024);

// Encrypt a plaintext batch and write all columns to storage.
void encryptAndStore(const PlaintextBatch& batch,
                     const TableSchema& schema,
                     FHEKeyContext& ctx,
                     EncryptedStorage& storage);

// Full pipeline: CSV → encrypt → store.
void ingestCSV(const std::string& csvPath,
               const TableSchema& schema,
               size_t chunkSize,
               FHEKeyContext& ctx,
               EncryptedStorage& storage);

} // namespace proto1
