// Quick debug test
#include "proto1/fhe_runtime.h"
#include "proto1/storage.h"
#include "proto1/ingestion.h"
#include "proto1/operators.h"
#include "proto1/result_stream.h"
#include "proto1/client.h"
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace proto1;
namespace fs = std::filesystem;

int main() {
    FHEParameterSet params;
    params.id                  = "bfv-257-depth8";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 8;
    params.ringDimension       = 128;

    auto clientCtx = createBFVContext(params);
    QueryClient client(clientCtx);

    // Create test data
    PlaintextVector pv;
    pv.values = {1, 2, 3, 4, 5, 6, 7, 8, 1, 5};
    auto encrypted = clientCtx->encrypt(pv);

    // Decrypt
    auto decrypted = client.decryptVector(*encrypted, 10);
    
    std::cout << "Original:  ";
    for (auto v : pv.values) std::cout << v << " ";
    std::cout << "\n";
    
    std::cout << "Decrypted: ";
    for (auto v : decrypted) std::cout << v << " ";
    std::cout << "\n";

    return 0;
}
