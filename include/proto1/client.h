#pragma once
// Trusted client: key management, query literal encryption, result decryption,
// and row reconstruction.

#include "proto1/fhe_runtime.h"
#include "proto1/encrypted_batch.h"
#include "proto1/expression.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto1 {

// A reconstructed plaintext row.
struct Row {
    std::unordered_map<std::string, int64_t> values;
};

class QueryClient {
public:
    explicit QueryClient(std::shared_ptr<ClientKeyContext> keyCtx);

    // Encrypt a literal value for use in a query predicate.
    std::shared_ptr<EncryptedVector> encryptLiteral(int64_t value) const;

    // Decrypt a result batch and reconstruct rows where mask == 1.
    std::vector<Row> decryptAndReconstruct(const EncryptedBatch& batch) const;

    // Decrypt a single EncryptedVector (for testing).
    std::vector<int64_t> decryptVector(const EncryptedVector& v,
                                       size_t logicalLen) const;

    const ClientKeyContext& keyContext() const { return *keyCtx_; }
    std::shared_ptr<ClientKeyContext> keyContextPtr() const { return keyCtx_; }

private:
    std::shared_ptr<ClientKeyContext> keyCtx_;
};

} // namespace proto1
