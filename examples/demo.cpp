// Prototype 1 — End-to-End Demo with Bitmask-Based Filtering
//
// Query: SELECT id, age, salary FROM employees WHERE id=5 AND age>3
//
// Flow:
// 1. Server executes WHERE clause homomorphically → encrypted bitmask
// 2. Client decrypts bitmask only (lightweight)
// 3. Client sends decrypted bitmask back to server
// 4. Server uses bitmask to filter and return only matching encrypted rows
// 5. Client decrypts and reconstructs rows
//
// Note: BFV with p=257 requires values in [0,128] for correct GT/LT semantics.
// The demo dataset uses values in [1,8].

#include "proto1/fhe_runtime.h"
#include "proto1/storage.h"
#include "proto1/ingestion.h"
#include "proto1/operators.h"
#include "proto1/result_stream.h"
#include "proto1/client.h"
#include "proto1/catalog.h"
#include "proto1/plan_validator.h"
#include "proto1/logger.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace proto1;
namespace fs = std::filesystem;

struct DemoRow { int64_t id, age, salary; };

static const DemoRow DATASET[10] = {
    {1, 2, 5},
    {2, 3, 6},
    {3, 4, 7},
    {4, 5, 8},
    {5, 6, 4},  // matches: id=5, age=6>3
    {6, 7, 3},
    {7, 8, 2},
    {8, 1, 1},
    {1, 2, 5},
    {5, 2, 8},  // id=5 but age=2 not >3
};

static void printRow(const Row& row) {
    std::cout << "  { ";
    bool first = true;
    for (auto& [k, v] : row.values) {
        if (!first) std::cout << ", ";
        std::cout << k << "=" << v;
        first = false;
    }
    std::cout << " }\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Prototype 1 — Bitmask-Based FHE Query Execution   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    // ── Step 1: FHE Setup ──────────────────────────────────────────────────
    std::cout << "Step 1: FHE Setup (BFV, p=257, depth=9, N=128)\n";
    FHEParameterSet params;
    params.id                  = "bfv-257-depth9";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 9;
    params.ringDimension       = 128;

    auto t1 = std::chrono::high_resolution_clock::now();
    auto clientCtx = createBFVContext(params);
    auto t2 = std::chrono::high_resolution_clock::now();
    auto keygenMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    std::cout << "  Slot count: " << clientCtx->slotCount() << "\n";
    std::cout << "  Key generation: " << keygenMs << " ms\n\n";

    // ── Step 2: Create dataset ─────────────────────────────────────────────
    std::cout << "Step 2: Create 10-row dataset\n";
    std::cout << "  Note: p=257 → values must be in [0,128] for correct GT/LT\n";
    std::cout << "  Query: SELECT id,age,salary WHERE id=5 AND age>3\n\n";

    auto dataDir = fs::temp_directory_path() / "proto1_demo";
    fs::remove_all(dataDir);
    fs::create_directories(dataDir);

    auto csvPath = (dataDir / "employees.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n";
        for (auto& row : DATASET)
            f << row.id << "," << row.age << "," << row.salary << "\n";
    }

    // Compute plaintext reference result
    std::vector<DemoRow> plainRef;
    for (auto& row : DATASET)
        if (row.id == 5 && row.age > 3)
            plainRef.push_back(row);

    std::cout << "  Plaintext reference result (" << plainRef.size() << " rows):\n";
    for (auto& r : plainRef)
        std::cout << "    id=" << r.id << " age=" << r.age << " salary=" << r.salary << "\n";
    std::cout << "\n";

    // ── Step 3: Encrypt and store ──────────────────────────────────────────
    std::cout << "Step 3: Encrypt and persist dataset\n";
    TableSchema schema{"employees",
        {{"id", DataType::INT64},
         {"age", DataType::INT64},
         {"salary", DataType::INT64}}};

    auto storage = makeFilesystemStorage((dataDir / "encrypted").string(), clientCtx);

    auto t3 = std::chrono::high_resolution_clock::now();
    ingestCSV(csvPath, schema, 10, *clientCtx, *storage);
    auto t4 = std::chrono::high_resolution_clock::now();
    auto encMs = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();
    std::cout << "  Encryption + storage: " << encMs << " ms\n\n";

    // ── Step 4: Build physical query plan ─────────────────────────────────
    std::cout << "Step 4: Build physical query plan\n";
    std::cout << "  BitmaskFilterExec(AND(EQ(id,5), GT(age,3)))\n";
    std::cout << "    └─ TableScanExec(employees)\n\n";

    QueryClient client(clientCtx);
    auto lit5 = client.encryptLiteral(5);
    auto lit3 = client.encryptLiteral(3);

    auto predicate = Expression::andExpr(
        Expression::equal(Expression::columnRef("id"),  Expression::encLiteral(lit5, 5)),
        Expression::gt(Expression::columnRef("age"),    Expression::encLiteral(lit3, 3)));

    Catalog catalog;
    catalog.registerTable(schema);
    PlanValidator validator(catalog);
    validator.validateExpression(*predicate, "employees");
    validator.validateProjection({"id", "age", "salary"}, "employees");
    std::cout << "  Plan validation: OK\n\n";

    // ── Step 5: Execute (bitmask-only) ─────────────────────────────────────
    std::cout << "Step 5: Execute encrypted query (bitmask-only)\n";

    auto scan    = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto bitmaskFilter = std::make_shared<BitmaskFilterExec>(scan, predicate, clientCtx);

    auto t5 = std::chrono::high_resolution_clock::now();
    EncryptedResultStream stream(bitmaskFilter);
    auto bitmaskBatches = stream.collectAll();
    auto t6 = std::chrono::high_resolution_clock::now();
    auto execMs = std::chrono::duration_cast<std::chrono::milliseconds>(t6 - t5).count();
    std::cout << "  Query execution (bitmask): " << execMs << " ms\n\n";

    // ── Step 6: Client decrypts bitmask ────────────────────────────────────
    std::cout << "Step 6: Client decrypts bitmask\n";
    std::vector<std::vector<int64_t>> decryptedBitmasks;
    for (auto& batch : bitmaskBatches) {
        auto bitmask = client.decryptBitmask(batch);
        decryptedBitmasks.push_back(bitmask);
        std::cout << "  Chunk " << batch.chunkId << ": ";
        int64_t matchCount = 0;
        for (auto b : bitmask) if (b == 1) ++matchCount;
        std::cout << matchCount << "/" << batch.logicalRowCount << " rows match\n";
    }
    std::cout << "\n";

    // ── Step 7: Server applies bitmask and returns encrypted rows ──────────
    std::cout << "Step 7: Server applies bitmask and returns encrypted rows\n";
    std::vector<EncryptedBatch> filteredBatches;
    for (size_t i = 0; i < bitmaskBatches.size(); ++i) {
        auto& batch = bitmaskBatches[i];
        auto& bitmask = decryptedBitmasks[i];
        auto filtered = storage->applyBitmaskFilter(
            "employees",
            std::vector<std::string>{"id", "age", "salary"},
            0,  // partitionId
            batch.chunkId,
            bitmask);
        filteredBatches.push_back(filtered);
    }
    std::cout << "  Returned " << filteredBatches.size() << " filtered batch(es)\n\n";

    // ── Step 8: Client decrypts and reconstructs rows ──────────────────────
    std::cout << "Step 8: Client decrypts and reconstructs rows\n";
    std::vector<Row> encResult;
    for (size_t i = 0; i < filteredBatches.size(); ++i) {
        auto& batch = filteredBatches[i];
        auto& bitmask = decryptedBitmasks[i];
        
        // Decrypt each column
        std::unordered_map<std::string, std::vector<int64_t>> decryptedCols;
        for (auto& [colName, chunk] : batch.columns) {
            if (chunk.ciphertexts.empty())
                throw std::runtime_error("Column has no ciphertexts: " + colName);
            auto pv = clientCtx->decrypt(*chunk.ciphertexts[0], batch.logicalRowCount);
            decryptedCols[colName] = pv.values;
        }
        
        // Reconstruct rows where bitmask[i] == 1
        for (size_t j = 0; j < bitmask.size(); ++j) {
            if (bitmask[j] == 1) {
                Row row;
                for (auto& [colName, vals] : decryptedCols)
                    row.values[colName] = vals[j];
                encResult.push_back(std::move(row));
            }
        }
    }

    std::cout << "  Encrypted query result (" << encResult.size() << " rows):\n";
    for (auto& row : encResult) printRow(row);
    std::cout << "\n";

    // ── Step 9: Verify ─────────────────────────────────────────────────────
    std::cout << "Step 9: Verify encrypted result matches plaintext reference\n";
    bool match = (encResult.size() == plainRef.size());
    if (match) {
        for (size_t i = 0; i < encResult.size() && match; ++i) {
            auto& r = encResult[i];
            auto& p = plainRef[i];
            match = (r.values.at("id")     == p.id  &&
                     r.values.at("age")    == p.age  &&
                     r.values.at("salary") == p.salary);
        }
    }

    if (match) {
        std::cout << "  ✓ PASS — encrypted result matches plaintext reference\n\n";
    } else {
        std::cout << "  ✗ FAIL — mismatch!\n  Expected:\n";
        for (auto& r : plainRef)
            std::cout << "    id=" << r.id << " age=" << r.age << " salary=" << r.salary << "\n";
        std::cout << "  Got:\n";
        for (auto& row : encResult) printRow(row);
        std::cout << "\n";
    }

    // ── Performance summary ────────────────────────────────────────────────
    auto tEnd = std::chrono::high_resolution_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - t0).count();

    std::cout << "Performance Summary:\n";
    std::cout << "  Key generation:    " << std::setw(6) << keygenMs << " ms\n";
    std::cout << "  Encryption+store:  " << std::setw(6) << encMs    << " ms\n";
    std::cout << "  Query execution:   " << std::setw(6) << execMs   << " ms\n";
    std::cout << "  Total:             " << std::setw(6) << totalMs  << " ms\n\n";

    auto chunks = storage->listChunks("employees", "id");
    if (!chunks.empty()) {
        auto chunk = storage->getChunk("employees", "id", 0, 0);
        auto bytes = chunk.ciphertexts[0]->serialize();
        size_t ptBytes = 10 * sizeof(int64_t);
        std::cout << "Storage Metrics (id column, chunk 0):\n";
        std::cout << "  Plaintext size:    " << ptBytes << " bytes\n";
        std::cout << "  Ciphertext size:   " << bytes.size() << " bytes\n";
        std::cout << "  Expansion ratio:   " << std::fixed << std::setprecision(1)
                  << (double)bytes.size() / ptBytes << "x\n\n";
    }

    fs::remove_all(dataDir);
    return match ? 0 : 1;
}
