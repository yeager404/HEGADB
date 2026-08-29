// CSV ingestion and encryption pipeline.

#include "proto1/ingestion.h"
#include "proto1/logger.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace proto1 {

std::vector<PlaintextBatch>
readCSV(const std::string& path,
        const TableSchema& schema,
        size_t chunkSize) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open CSV: " + path);

    // Parse header
    std::string line;
    if (!std::getline(f, line))
        throw std::runtime_error("CSV is empty: " + path);

    std::vector<std::string> headers;
    std::istringstream hss(line);
    std::string tok;
    while (std::getline(hss, tok, ',')) {
        // Trim whitespace
        tok.erase(0, tok.find_first_not_of(" \t\r\n"));
        tok.erase(tok.find_last_not_of(" \t\r\n") + 1);
        headers.push_back(tok);
    }

    // Validate headers match schema
    for (auto& col : schema.columns) {
        bool found = false;
        for (auto& h : headers)
            if (h == col.name) { found = true; break; }
        if (!found)
            throw std::runtime_error("CSV missing column: " + col.name);
    }

    // Read rows into batches
    std::vector<PlaintextBatch> batches;
    PlaintextBatch current;
    current.tableId = schema.tableId;
    current.chunkId = 0;
    current.logicalRowStart = 0;

    int64_t totalRows = 0;

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream rss(line);
        std::vector<std::string> fields;
        while (std::getline(rss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t\r\n"));
            tok.erase(tok.find_last_not_of(" \t\r\n") + 1);
            fields.push_back(tok);
        }
        if (fields.size() != headers.size())
            throw std::runtime_error("CSV row has wrong field count");

        for (size_t i = 0; i < headers.size(); ++i) {
            // Only store columns that are in the schema
            for (auto& col : schema.columns) {
                if (col.name == headers[i]) {
                    current.columns[col.name].push_back(std::stoll(fields[i]));
                    break;
                }
            }
        }
        ++totalRows;

        if (current.rowCount() >= chunkSize) {
            batches.push_back(std::move(current));
            current = PlaintextBatch{};
            current.tableId = schema.tableId;
            current.chunkId = static_cast<uint32_t>(batches.size());
            current.logicalRowStart = totalRows;
        }
    }

    if (current.rowCount() > 0)
        batches.push_back(std::move(current));

    LOG_INFO("Ingestion", "readCSV: " + path + " → " +
             std::to_string(totalRows) + " rows, " +
             std::to_string(batches.size()) + " chunks");
    return batches;
}

void encryptAndStore(const PlaintextBatch& batch,
                     const TableSchema& schema,
                     FHEKeyContext& ctx,
                     EncryptedStorage& storage) {
    size_t slotCount = ctx.slotCount();
    size_t rowCount  = batch.rowCount();

    for (auto& col : schema.columns) {
        auto it = batch.columns.find(col.name);
        if (it == batch.columns.end()) continue;

        auto& vals = it->second;

        // Pad to slot count with zeros (padding doesn't affect correctness
        // because the selection mask will be 0 for padding slots).
        PlaintextVector pv;
        pv.values = vals;
        pv.values.resize(slotCount, 0);

        auto ct = ctx.encrypt(pv);

        EncryptedColumnChunk chunk;
        chunk.meta.tableId         = batch.tableId;
        chunk.meta.columnId        = col.name;
        chunk.meta.partitionId     = 0;
        chunk.meta.chunkId         = batch.chunkId;
        chunk.meta.logicalRowStart = batch.logicalRowStart;
        chunk.meta.logicalRowCount = static_cast<int64_t>(rowCount);
        chunk.meta.paramSetId      = ctx.paramSetId();
        chunk.meta.keyId           = "key-0"; // Prototype 1: single key
        chunk.meta.slotCount       = slotCount;
        chunk.meta.ciphertextCount = 1;
        chunk.ciphertexts.push_back(ct);

        storage.putChunk(chunk);
    }
}

void ingestCSV(const std::string& csvPath,
               const TableSchema& schema,
               size_t chunkSize,
               FHEKeyContext& ctx,
               EncryptedStorage& storage) {
    auto batches = readCSV(csvPath, schema, chunkSize);
    for (auto& batch : batches)
        encryptAndStore(batch, schema, ctx, storage);
    LOG_INFO("Ingestion", "ingestCSV complete: " + std::to_string(batches.size()) + " chunks stored");
}

} // namespace proto1
