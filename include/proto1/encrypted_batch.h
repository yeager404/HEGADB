#pragma once
// EncryptedColumnChunk and EncryptedBatch — the core columnar encrypted data model.

#include "proto1/fhe_runtime.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto1 {

// Metadata that travels with every encrypted chunk.
struct ChunkMeta {
    std::string tableId;
    std::string columnId;
    uint32_t    partitionId{0};
    uint32_t    chunkId{0};

    int64_t  logicalRowStart{0};
    int64_t  logicalRowCount{0};

    std::string paramSetId;   // must match FHEKeyContext::paramSetId()
    std::string keyId;        // public key fingerprint (non-secret)
    size_t      slotCount{0}; // slots per ciphertext
    size_t      ciphertextCount{0};
};

// One logical column chunk = metadata + N ciphertexts.
// Slot i across all ciphertexts in the same chunk corresponds to the same
// logical row (within the ciphertext's window).
struct EncryptedColumnChunk {
    ChunkMeta meta;
    std::vector<std::shared_ptr<EncryptedVector>> ciphertexts;

    // Validate internal consistency.
    void validate() const;
};

// A batch of aligned column chunks ready for execution.
// All columns must have the same chunkId, partitionId, tableId, and
// logicalRowCount.
struct EncryptedBatch {
    uint32_t chunkId{0};
    int64_t  logicalRowCount{0};

    // column name → chunk
    std::unordered_map<std::string, EncryptedColumnChunk> columns;

    // Optional selection mask produced by FilterExec.
    // nullptr means "all rows selected".
    std::shared_ptr<EncryptedVector> selectionMask;

    // Human-readable predicate string set by FilterExec for display purposes.
    std::string predicateSQL;

    // Validate that all columns are aligned.
    void validate() const;
};

} // namespace proto1
