// Test: BitmaskFilterExec Operator
// Verifies that BitmaskFilterExec returns only the encrypted bitmask

#include "proto1/fhe_runtime.h"
#include "proto1/storage.h"
#include "proto1/ingestion.h"
#include "proto1/operators.h"
#include "proto1/result_stream.h"
#include "proto1/client.h"
#include "proto1/catalog.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>

using namespace proto1;
namespace fs = std::filesystem;

int main() {
    std::cout << "\n=== Test: BitmaskFilterExec Operator ===\n\n";

    // Setup
    FHEParameterSet params;
    params.id                  = "bfv-257-depth8";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 8;
    params.ringDimension       = 128;

    auto clientCtx = createBFVContext(params);
    QueryClient client(clientCtx);

    // Create test data
    auto dataDir = fs::temp_directory_path() / "proto1_test_bitmask";
    fs::remove_all(dataDir);
    fs::create_directories(dataDir);

    auto csvPath = (dataDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,value\n";
        f << "1,10\n";
        f << "2,20\n";
        f << "3,30\n";
        f << "4,40\n";
        f << "5,50\n";
    }

    // Encrypt and store
    TableSchema schema{"test", {{"id", DataType::INT64}, {"value", DataType::INT64}}};
    auto storage = makeFilesystemStorage((dataDir / "encrypted").string(), clientCtx);
    ingestCSV(csvPath, schema, 5, *clientCtx, *storage);

    // Build query plan: BitmaskFilterExec(id > 2)
    auto lit2 = client.encryptLiteral(2);
    auto predicate = Expression::gt(
        Expression::columnRef("id"),
        Expression::encLiteral(lit2));

    Catalog catalog;
    catalog.registerTable(schema);

    auto scan = std::make_shared<TableScanExec>(
        "test", std::vector<std::string>{"id", "value"}, storage);
    auto bitmaskFilter = std::make_shared<BitmaskFilterExec>(scan, predicate, clientCtx);

    // Execute
    EncryptedResultStream stream(bitmaskFilter);
    auto batches = stream.collectAll();

    std::cout << "Batches returned: " << batches.size() << "\n";
    assert(batches.size() == 1);

    auto& batch = batches[0];
    std::cout << "Batch logicalRowCount: " << batch.logicalRowCount << "\n";
    assert(batch.logicalRowCount == 5);

    // Verify: batch should have ONLY selectionMask, no columns
    std::cout << "Columns in batch: " << batch.columns.size() << "\n";
    assert(batch.columns.size() == 0);  // No columns!

    std::cout << "Has selectionMask: " << (batch.selectionMask != nullptr ? "yes" : "no") << "\n";
    assert(batch.selectionMask != nullptr);

    // Decrypt bitmask
    auto bitmask = client.decryptBitmask(batch);
    std::cout << "Bitmask: ";
    for (auto b : bitmask) std::cout << b << " ";
    std::cout << "\n\n";

    // Verify: should be [0, 0, 1, 1, 1] (id > 2 means id = 3, 4, 5)
    bool match = (bitmask.size() == 5 &&
                  bitmask[0] == 0 &&
                  bitmask[1] == 0 &&
                  bitmask[2] == 1 &&
                  bitmask[3] == 1 &&
                  bitmask[4] == 1);

    if (match) {
        std::cout << "✓ PASS: BitmaskFilterExec returns only bitmask\n\n";
        fs::remove_all(dataDir);
        return 0;
    } else {
        std::cout << "✗ FAIL: Bitmask incorrect\n\n";
        fs::remove_all(dataDir);
        return 1;
    }
}
