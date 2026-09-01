// Filesystem-based EncryptedStorage implementation.
//
// Layout:
//   <root>/<tableId>/<columnId>/p<partitionId>/chunk_<chunkId>.bin
//   <root>/<tableId>/<columnId>/p<partitionId>/chunk_<chunkId>.meta.json
//
// Binary format for .bin:
//   [uint32_t count]
//   for each ciphertext:
//     [uint64_t logicalSize]
//     [uint64_t byteLen]
//     [byteLen bytes]

#include "proto1/storage.h"
#include "proto1/logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace proto1 {

namespace fs = std::filesystem;

// ── Minimal JSON helpers ───────────────────────────────────────────────────

static std::string metaToJson(const ChunkMeta& m) {
    std::ostringstream o;
    o << "{\n"
      << "  \"tableId\": \""         << m.tableId         << "\",\n"
      << "  \"columnId\": \""        << m.columnId        << "\",\n"
      << "  \"partitionId\": "       << m.partitionId     << ",\n"
      << "  \"chunkId\": "           << m.chunkId         << ",\n"
      << "  \"logicalRowStart\": "   << m.logicalRowStart << ",\n"
      << "  \"logicalRowCount\": "   << m.logicalRowCount << ",\n"
      << "  \"paramSetId\": \""      << m.paramSetId      << "\",\n"
      << "  \"keyId\": \""           << m.keyId           << "\",\n"
      << "  \"slotCount\": "         << m.slotCount       << ",\n"
      << "  \"ciphertextCount\": "   << m.ciphertextCount << "\n"
      << "}\n";
    return o.str();
}

static std::string extractStr(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) throw std::runtime_error("JSON key not found: " + key);
    pos = json.find(':', pos);
    pos = json.find('"', pos);
    auto end = json.find('"', pos + 1);
    return json.substr(pos + 1, end - pos - 1);
}

static int64_t extractInt(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) throw std::runtime_error("JSON key not found: " + key);
    pos = json.find(':', pos) + 1;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n')) ++pos;
    size_t end = pos;
    while (end < json.size() && (std::isdigit((unsigned char)json[end]) || json[end] == '-')) ++end;
    return std::stoll(json.substr(pos, end - pos));
}

static ChunkMeta metaFromJson(const std::string& json) {
    ChunkMeta m;
    m.tableId         = extractStr(json, "tableId");
    m.columnId        = extractStr(json, "columnId");
    m.partitionId     = static_cast<uint32_t>(extractInt(json, "partitionId"));
    m.chunkId         = static_cast<uint32_t>(extractInt(json, "chunkId"));
    m.logicalRowStart = extractInt(json, "logicalRowStart");
    m.logicalRowCount = extractInt(json, "logicalRowCount");
    m.paramSetId      = extractStr(json, "paramSetId");
    m.keyId           = extractStr(json, "keyId");
    m.slotCount       = static_cast<size_t>(extractInt(json, "slotCount"));
    m.ciphertextCount = static_cast<size_t>(extractInt(json, "ciphertextCount"));
    return m;
}

// ── Path helpers ───────────────────────────────────────────────────────────

static std::string chunkDir(const std::string& root,
                             const std::string& tableId,
                             const std::string& columnId,
                             uint32_t partitionId) {
    return root + "/" + tableId + "/" + columnId + "/p" + std::to_string(partitionId);
}

static std::string chunkBinPath(const std::string& root,
                                 const std::string& tableId,
                                 const std::string& columnId,
                                 uint32_t partitionId,
                                 uint32_t chunkId) {
    return chunkDir(root, tableId, columnId, partitionId)
           + "/chunk_" + std::to_string(chunkId) + ".bin";
}

static std::string chunkMetaPath(const std::string& root,
                                  const std::string& tableId,
                                  const std::string& columnId,
                                  uint32_t partitionId,
                                  uint32_t chunkId) {
    return chunkDir(root, tableId, columnId, partitionId)
           + "/chunk_" + std::to_string(chunkId) + ".meta.json";
}

// ── FilesystemStorage ──────────────────────────────────────────────────────

class FilesystemStorage : public EncryptedStorage {
public:
    FilesystemStorage(std::string root, std::shared_ptr<FHEKeyContext> ctx)
        : root_(std::move(root)), ctx_(std::move(ctx)) {
        fs::create_directories(root_);
    }

    void putChunk(const EncryptedColumnChunk& chunk) override {
        chunk.validate();
        auto& m = chunk.meta;
        auto dir = chunkDir(root_, m.tableId, m.columnId, m.partitionId);
        fs::create_directories(dir);

        auto binPath = chunkBinPath(root_, m.tableId, m.columnId, m.partitionId, m.chunkId);
        std::ofstream f(binPath, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot write: " + binPath);

        uint32_t count = static_cast<uint32_t>(chunk.ciphertexts.size());
        f.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (auto& ct : chunk.ciphertexts) {
            uint64_t logSz   = ct->logicalSize();
            auto     bytes   = ct->serialize();
            uint64_t byteLen = bytes.size();
            f.write(reinterpret_cast<const char*>(&logSz),   sizeof(logSz));
            f.write(reinterpret_cast<const char*>(&byteLen), sizeof(byteLen));
            f.write(reinterpret_cast<const char*>(bytes.data()), byteLen);
        }

        auto metaPath = chunkMetaPath(root_, m.tableId, m.columnId, m.partitionId, m.chunkId);
        std::ofstream mf(metaPath);
        if (!mf) throw std::runtime_error("Cannot write meta: " + metaPath);
        mf << metaToJson(m);

        LOG_DEBUG("Storage", "putChunk: " + m.tableId + "/" + m.columnId +
                  " chunk=" + std::to_string(m.chunkId) +
                  " rows=" + std::to_string(m.logicalRowCount));
    }

    EncryptedColumnChunk
    getChunk(const std::string& tableId,
             const std::string& columnId,
             uint32_t partitionId,
             uint32_t chunkId) const override {
        auto metaPath = chunkMetaPath(root_, tableId, columnId, partitionId, chunkId);
        auto binPath  = chunkBinPath(root_, tableId, columnId, partitionId, chunkId);

        if (!fs::exists(metaPath))
            throw std::runtime_error("Chunk meta not found: " + metaPath);
        if (!fs::exists(binPath))
            throw std::runtime_error("Chunk data not found: " + binPath);

        std::ifstream mf(metaPath);
        std::string jsonStr((std::istreambuf_iterator<char>(mf)),
                             std::istreambuf_iterator<char>());
        EncryptedColumnChunk chunk;
        chunk.meta = metaFromJson(jsonStr);

        std::ifstream f(binPath, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot read: " + binPath);

        uint32_t count;
        f.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (uint32_t i = 0; i < count; ++i) {
            uint64_t logSz, byteLen;
            f.read(reinterpret_cast<char*>(&logSz),   sizeof(logSz));
            f.read(reinterpret_cast<char*>(&byteLen), sizeof(byteLen));
            std::vector<uint8_t> bytes(byteLen);
            f.read(reinterpret_cast<char*>(bytes.data()), byteLen);

            auto ct = ctx_->deserializeWithSize(bytes, static_cast<size_t>(logSz));
            chunk.ciphertexts.push_back(ct);
        }

        LOG_DEBUG("Storage", "getChunk: " + tableId + "/" + columnId +
                  " chunk=" + std::to_string(chunkId));
        return chunk;
    }

    std::vector<ChunkMeta>
    listChunks(const std::string& tableId,
               const std::string& columnId) const override {
        std::vector<ChunkMeta> result;
        auto colDir = root_ + "/" + tableId + "/" + columnId;
        if (!fs::exists(colDir)) return result;

        for (auto& partEntry : fs::directory_iterator(colDir)) {
            if (!partEntry.is_directory()) continue;
            for (auto& entry : fs::directory_iterator(partEntry.path())) {
                auto name = entry.path().filename().string();
                if (name.size() > 10 &&
                    name.substr(name.size() - 10) == ".meta.json") {
                    std::ifstream mf2(entry.path());
                    std::string js((std::istreambuf_iterator<char>(mf2)),
                                    std::istreambuf_iterator<char>());
                    result.push_back(metaFromJson(js));
                }
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const ChunkMeta& a, const ChunkMeta& b) {
                      return a.chunkId < b.chunkId;
                  });
        return result;
    }

    ChunkMeta
    getChunkMetadata(const std::string& tableId,
                     const std::string& columnId,
                     uint32_t partitionId,
                     uint32_t chunkId) const override {
        auto metaPath = chunkMetaPath(root_, tableId, columnId, partitionId, chunkId);
        std::ifstream mf(metaPath);
        if (!mf) throw std::runtime_error("Meta not found: " + metaPath);
        std::string js((std::istreambuf_iterator<char>(mf)),
                        std::istreambuf_iterator<char>());
        return metaFromJson(js);
    }

    EncryptedBatch
    applyBitmaskFilter(const std::string& tableId,
                       const std::vector<std::string>& columnIds,
                       uint32_t partitionId,
                       uint32_t chunkId,
                       const std::vector<int64_t>& bitmask) const override {
        if (columnIds.empty())
            throw std::runtime_error("applyBitmaskFilter: no columns specified");

        EncryptedBatch result;
        result.chunkId = chunkId;

        // Load all requested columns
        std::unordered_map<std::string, EncryptedColumnChunk> allCols;
        for (auto& colId : columnIds) {
            allCols[colId] = getChunk(tableId, colId, partitionId, chunkId);
        }

        // Get row count from first column
        auto& firstChunk = allCols.begin()->second;
        int64_t totalRows = firstChunk.meta.logicalRowCount;

        if (bitmask.size() != static_cast<size_t>(totalRows))
            throw std::runtime_error("Bitmask size mismatch");

        // Keep logicalRowCount as original (client will filter using bitmask)
        result.logicalRowCount = totalRows;

        // Return all columns unchanged
        for (auto& [colId, chunk] : allCols) {
            result.columns[colId] = std::move(chunk);
        }

        LOG_DEBUG("Storage", "applyBitmaskFilter: " + tableId +
                  " chunk=" + std::to_string(chunkId) +
                  " rows=" + std::to_string(totalRows));

        return result;
    }

private:
    std::string                    root_;
    std::shared_ptr<FHEKeyContext> ctx_;
};

std::shared_ptr<EncryptedStorage>
makeFilesystemStorage(const std::string& rootDir,
                      std::shared_ptr<FHEKeyContext> ctx) {
    return std::make_shared<FilesystemStorage>(rootDir, std::move(ctx));
}

} // namespace proto1
