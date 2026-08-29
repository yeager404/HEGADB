#pragma once
// Storage abstraction — the execution engine depends only on this interface.

#include "proto1/encrypted_batch.h"
#include <memory>
#include <string>
#include <vector>

namespace proto1 {

class EncryptedStorage {
public:
    virtual ~EncryptedStorage() = default;

    virtual void putChunk(const EncryptedColumnChunk& chunk) = 0;

    virtual EncryptedColumnChunk
    getChunk(const std::string& tableId,
             const std::string& columnId,
             uint32_t partitionId,
             uint32_t chunkId) const = 0;

    virtual std::vector<ChunkMeta>
    listChunks(const std::string& tableId,
               const std::string& columnId) const = 0;

    virtual ChunkMeta
    getChunkMetadata(const std::string& tableId,
                     const std::string& columnId,
                     uint32_t partitionId,
                     uint32_t chunkId) const = 0;
};

// Filesystem implementation.
std::shared_ptr<EncryptedStorage>
makeFilesystemStorage(const std::string& rootDir,
                      std::shared_ptr<FHEKeyContext> ctx);

} // namespace proto1
