// EncryptedColumnChunk and EncryptedBatch validation.

#include "proto1/encrypted_batch.h"
#include <stdexcept>
#include <string>

namespace proto1 {

void EncryptedColumnChunk::validate() const {
    if (meta.tableId.empty())
        throw std::runtime_error("EncryptedColumnChunk: empty tableId");
    if (meta.columnId.empty())
        throw std::runtime_error("EncryptedColumnChunk: empty columnId");
    if (meta.logicalRowCount <= 0)
        throw std::runtime_error("EncryptedColumnChunk: logicalRowCount <= 0");
    if (ciphertexts.empty())
        throw std::runtime_error("EncryptedColumnChunk: no ciphertexts");
    if (meta.ciphertextCount != ciphertexts.size())
        throw std::runtime_error("EncryptedColumnChunk: ciphertextCount mismatch");
    for (auto& ct : ciphertexts)
        if (!ct)
            throw std::runtime_error("EncryptedColumnChunk: null ciphertext");
}

void EncryptedBatch::validate() const {
    if (columns.empty())
        throw std::runtime_error("EncryptedBatch: no columns");

    for (auto& [name, chunk] : columns) {
        if (chunk.meta.chunkId != chunkId)
            throw std::runtime_error("EncryptedBatch: chunkId mismatch for column " + name);
        if (chunk.meta.logicalRowCount != logicalRowCount)
            throw std::runtime_error("EncryptedBatch: logicalRowCount mismatch for column " + name);
    }
}

} // namespace proto1
