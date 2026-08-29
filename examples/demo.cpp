// Prototype 1 — End-to-End Demo
//
// Query: SELECT id, age, salary FROM employees WHERE id=5 AND age>3
//
// Note: BFV with p=17 requires values in [0,8] for correct GT/LT semantics.
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
    std::cout << "║  Prototype 1 — FHE Database Execution Engine Demo   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    // ── Step 1: FHE Setup ──────────────────────────────────────────────────
    std::cout << "Step 1: FHE Setup (BFV, p=257, depth=8, N=128)\n";
    FHEParameterSet params;
    params.id                  = "bfv-257-depth8";
    params.plaintextModulus    = 257;
    params.multiplicativeDepth = 8;
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
    std::cout << "  ProjectExec([id, age, salary])\n";
    std::cout << "    └─ FilterExec(AND(EQ(id,5), GT(age,3)))\n";
    std::cout << "         └─ TableScanExec(employees)\n\n";

    QueryClient client(clientCtx);
    auto lit5 = client.encryptLiteral(5);
    auto lit3 = client.encryptLiteral(3);

    auto predicate = Expression::andExpr(
        Expression::equal(Expression::columnRef("id"),  Expression::encLiteral(lit5)),
        Expression::gt(Expression::columnRef("age"),    Expression::encLiteral(lit3)));

    Catalog catalog;
    catalog.registerTable(schema);
    PlanValidator validator(catalog);
    validator.validateExpression(*predicate, "employees");
    validator.validateProjection({"id", "age", "salary"}, "employees");
    std::cout << "  Plan validation: OK\n\n";

    // ── Step 5: Execute ────────────────────────────────────────────────────
    std::cout << "Step 5: Execute encrypted query\n";

    auto scan    = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto filter  = std::make_shared<FilterExec>(scan, predicate, clientCtx);
    auto project = std::make_shared<ProjectExec>(
        filter, std::vector<std::string>{"id", "age", "salary"});

    auto t5 = std::chrono::high_resolution_clock::now();
    EncryptedResultStream stream(project);
    auto resultBatches = stream.collectAll();
    auto t6 = std::chrono::high_resolution_clock::now();
    auto execMs = std::chrono::duration_cast<std::chrono::milliseconds>(t6 - t5).count();
    std::cout << "  Query execution: " << execMs << " ms\n\n";

    // ── Step 6: Decrypt and reconstruct ───────────────────────────────────
    std::cout << "Step 6: Client decrypts and reconstructs rows\n";
    std::vector<Row> encResult;
    for (auto& batch : resultBatches) {
        auto rows = client.decryptAndReconstruct(batch);
        encResult.insert(encResult.end(), rows.begin(), rows.end());
    }

    std::cout << "  Encrypted query result (" << encResult.size() << " rows):\n";
    for (auto& row : encResult) printRow(row);
    std::cout << "\n";

    // ── Step 7: Verify ─────────────────────────────────────────────────────
    std::cout << "Step 7: Verify encrypted result matches plaintext reference\n";
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
