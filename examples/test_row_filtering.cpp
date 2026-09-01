// Test: Row Filtering with Bitmask
// Verifies that rows are correctly filtered based on decrypted bitmask

#include "proto1/fhe_runtime.h"
#include "proto1/client.h"
#include "proto1/encrypted_batch.h"
#include <iostream>
#include <cassert>

using namespace proto1;

int main() {
    std::cout << "\n=== Test: Row Filtering with Bitmask ===\n\n";

    // Setup
    FHEParameterSet params;
    params.id                  = "bfv-257-depth8";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 8;
    params.ringDimension       = 128;

    auto clientCtx = createBFVContext(params);
    QueryClient client(clientCtx);

    // Create test data: 5 rows with id column
    PlaintextVector idPv;
    idPv.values = {10, 20, 30, 40, 50};

    // Create bitmask: [1, 0, 1, 0, 1] (keep rows 0, 2, 4)
    PlaintextVector maskPv;
    maskPv.values = {1, 0, 1, 0, 1};

    // Encrypt
    auto encryptedId = clientCtx->encrypt(idPv);
    auto encryptedMask = clientCtx->encrypt(maskPv);

    // Create batch
    EncryptedBatch batch;
    batch.chunkId = 0;
    batch.logicalRowCount = 5;
    batch.selectionMask = encryptedMask;

    EncryptedColumnChunk idChunk;
    idChunk.meta.tableId = "test";
    idChunk.meta.columnId = "id";
    idChunk.meta.logicalRowCount = 5;
    idChunk.ciphertexts.push_back(encryptedId);
    batch.columns["id"] = idChunk;

    // Decrypt
    auto decryptedMask = client.decryptBitmask(batch);
    auto decryptedId = client.decryptVector(*encryptedId, 5);

    // Filter rows
    std::cout << "Original rows:  ";
    for (auto v : decryptedId) std::cout << v << " ";
    std::cout << "\n";

    std::cout << "Bitmask:        ";
    for (auto v : decryptedMask) std::cout << v << " ";
    std::cout << "\n";

    std::vector<int64_t> filtered;
    for (size_t i = 0; i < decryptedMask.size(); ++i) {
        if (decryptedMask[i] == 1) {
            filtered.push_back(decryptedId[i]);
        }
    }

    std::cout << "Filtered rows:  ";
    for (auto v : filtered) std::cout << v << " ";
    std::cout << "\n\n";

    // Verify: should have rows with id = 10, 30, 50
    bool match = (filtered.size() == 3 &&
                  filtered[0] == 10 &&
                  filtered[1] == 30 &&
                  filtered[2] == 50);

    if (match) {
        std::cout << "✓ PASS: Row filtering correct\n\n";
        return 0;
    } else {
        std::cout << "✗ FAIL: Row filtering mismatch\n\n";
        return 1;
    }
}
