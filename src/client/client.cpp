// QueryClient implementation.

#include "proto1/client.h"
#include <stdexcept>

namespace proto1 {

QueryClient::QueryClient(std::shared_ptr<ClientKeyContext> keyCtx)
    : keyCtx_(std::move(keyCtx)) {}

std::shared_ptr<EncryptedVector>
QueryClient::encryptLiteral(int64_t value) const {
    // Broadcast the scalar to all slots so it can be compared element-wise.
    PlaintextVector pv;
    pv.values.assign(keyCtx_->slotCount(), value);
    return keyCtx_->encrypt(pv);
}

std::vector<int64_t>
QueryClient::decryptBitmask(const EncryptedBatch& batch) const {
    if (!batch.selectionMask) {
        // No filter applied → all rows selected
        return std::vector<int64_t>(batch.logicalRowCount, 1);
    }
    auto pv = keyCtx_->decrypt(*batch.selectionMask, batch.logicalRowCount);
    return pv.values;
}

std::vector<int64_t>
QueryClient::decryptVector(const EncryptedVector& v, size_t logicalLen) const {
    auto pv = keyCtx_->decrypt(v, logicalLen);
    return pv.values;
}

std::vector<Row>
QueryClient::decryptAndReconstruct(const EncryptedBatch& batch) const {
    size_t rowCount = static_cast<size_t>(batch.logicalRowCount);

    // Decrypt the selection mask.
    std::vector<int64_t> mask;
    if (batch.selectionMask) {
        auto pv = keyCtx_->decrypt(*batch.selectionMask, rowCount);
        mask = pv.values;
    } else {
        mask.assign(rowCount, 1); // no filter → all rows selected
    }

    // Decrypt each column.
    std::unordered_map<std::string, std::vector<int64_t>> decryptedCols;
    for (auto& [colName, chunk] : batch.columns) {
        if (chunk.ciphertexts.empty())
            throw std::runtime_error("Column has no ciphertexts: " + colName);
        // For Prototype 1: one ciphertext per chunk.
        auto pv = keyCtx_->decrypt(*chunk.ciphertexts[0], rowCount);
        decryptedCols[colName] = pv.values;
    }

    // Reconstruct rows where mask == 1.
    std::vector<Row> rows;
    for (size_t i = 0; i < rowCount; ++i) {
        if (mask[i] == 1) {
            Row row;
            for (auto& [colName, vals] : decryptedCols)
                row.values[colName] = vals[i];
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

} // namespace proto1
