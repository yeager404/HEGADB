// Test: Bitmask Decryption
// Verifies that decryptBitmask() correctly decrypts the selection mask

#include "proto1/fhe_runtime.h"
#include "proto1/client.h"
#include "proto1/encrypted_batch.h"
#include <iostream>
#include <cassert>

using namespace proto1;

int main() {
    std::cout << "\n=== Test: Bitmask Decryption ===\n\n";

    // Setup
    FHEParameterSet params;
    params.id                  = "bfv-257-depth8";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 8;
    params.ringDimension       = 128;

    auto clientCtx = createBFVContext(params);
    QueryClient client(clientCtx);

    // Create a test bitmask: [1, 0, 1, 0, 1, 0, 0, 0, 0, 0]
    PlaintextVector maskPv;
    maskPv.values = {1, 0, 1, 0, 1, 0, 0, 0, 0, 0};
    auto encryptedMask = clientCtx->encrypt(maskPv);

    // Create a batch with the encrypted mask
    EncryptedBatch batch;
    batch.chunkId = 0;
    batch.logicalRowCount = 10;
    batch.selectionMask = encryptedMask;

    // Decrypt the bitmask
    auto decryptedMask = client.decryptBitmask(batch);

    // Verify
    std::cout << "Original mask:   ";
    for (auto v : maskPv.values) std::cout << v << " ";
    std::cout << "\n";

    std::cout << "Decrypted mask:  ";
    for (auto v : decryptedMask) std::cout << v << " ";
    std::cout << "\n\n";

    bool match = (decryptedMask.size() == maskPv.values.size());
    for (size_t i = 0; i < decryptedMask.size() && match; ++i) {
        match = (decryptedMask[i] == maskPv.values[i]);
    }

    if (match) {
        std::cout << "✓ PASS: Bitmask decryption correct\n\n";
        return 0;
    } else {
        std::cout << "✗ FAIL: Bitmask mismatch\n\n";
        return 1;
    }
}
