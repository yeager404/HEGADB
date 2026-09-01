// Tests for storage, ingestion, operators, and end-to-end query execution.

#include <gtest/gtest.h>
#include "proto1/fhe_runtime.h"
#include "proto1/storage.h"
#include "proto1/ingestion.h"
#include "proto1/operators.h"
#include "proto1/result_stream.h"
#include "proto1/client.h"
#include "proto1/catalog.h"
#include "proto1/plan_validator.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace proto1;
namespace fs = std::filesystem;

static FHEParameterSet testParams() {
    FHEParameterSet p;
    p.id                  = "bfv-257-depth9";
    p.plaintextModulus    = 257;
    p.multiplicativeDepth = 9;
    p.ringDimension       = 128;
    return p;
}

static TableSchema employeeSchema() {
    return TableSchema{
        "employees",
        {{"id", DataType::INT64},
         {"age", DataType::INT64},
         {"salary", DataType::INT64}}
    };
}

class StorageTest : public ::testing::Test {
protected:
    std::shared_ptr<ClientKeyContext> ctx;
    std::shared_ptr<EncryptedStorage> storage;
    fs::path tmpDir;

    void SetUp() override {
        tmpDir = fs::temp_directory_path() / "proto1_storage_test";
        fs::remove_all(tmpDir);
        ctx     = createBFVContext(testParams());
        storage = makeFilesystemStorage(tmpDir.string(), ctx);
    }
    void TearDown() override { fs::remove_all(tmpDir); }
};

// ── Test 14: Encrypted chunk persistence ──────────────────────────────────
TEST_F(StorageTest, ChunkPersistence) {
    PlaintextVector pv;
    pv.values.assign(ctx->slotCount(), 0);
    pv.values[0] = 5; pv.values[1] = 3; pv.values[2] = 7;
    auto ct = ctx->encrypt(pv);

    EncryptedColumnChunk chunk;
    chunk.meta.tableId         = "employees";
    chunk.meta.columnId        = "id";
    chunk.meta.partitionId     = 0;
    chunk.meta.chunkId         = 0;
    chunk.meta.logicalRowStart = 0;
    chunk.meta.logicalRowCount = 3;
    chunk.meta.paramSetId      = ctx->paramSetId();
    chunk.meta.keyId           = "key-0";
    chunk.meta.slotCount       = ctx->slotCount();
    chunk.meta.ciphertextCount = 1;
    chunk.ciphertexts.push_back(ct);

    storage->putChunk(chunk);

    auto loaded = storage->getChunk("employees", "id", 0, 0);
    EXPECT_EQ(loaded.meta.logicalRowCount, 3);
    EXPECT_EQ(loaded.ciphertexts.size(), 1u);

    auto result = ctx->decrypt(*loaded.ciphertexts[0], 3);
    EXPECT_EQ(result.values[0], 5);
    EXPECT_EQ(result.values[1], 3);
    EXPECT_EQ(result.values[2], 7);
}

// ── Test 15: Chunk listing ─────────────────────────────────────────────────
TEST_F(StorageTest, ChunkListing) {
    for (int i = 0; i < 3; ++i) {
        PlaintextVector pv;
        pv.values.assign(ctx->slotCount(), i);
        auto ct = ctx->encrypt(pv);

        EncryptedColumnChunk chunk;
        chunk.meta.tableId         = "employees";
        chunk.meta.columnId        = "age";
        chunk.meta.partitionId     = 0;
        chunk.meta.chunkId         = static_cast<uint32_t>(i);
        chunk.meta.logicalRowStart = i * 5;
        chunk.meta.logicalRowCount = 5;
        chunk.meta.paramSetId      = ctx->paramSetId();
        chunk.meta.keyId           = "key-0";
        chunk.meta.slotCount       = ctx->slotCount();
        chunk.meta.ciphertextCount = 1;
        chunk.ciphertexts.push_back(ct);
        storage->putChunk(chunk);
    }

    auto list = storage->listChunks("employees", "age");
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].chunkId, 0u);
    EXPECT_EQ(list[1].chunkId, 1u);
    EXPECT_EQ(list[2].chunkId, 2u);
}

// ── Test 16: CSV ingestion ─────────────────────────────────────────────────
TEST_F(StorageTest, CSVIngestion) {
    // Write a small CSV
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }

    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    auto chunks = storage->listChunks("employees", "id");
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].logicalRowCount, 3);
}

// ── Test 17: TableScanExec ─────────────────────────────────────────────────
TEST_F(StorageTest, TableScanExec) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    auto scan = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age"}, storage);

    auto batch = scan->next();
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->logicalRowCount, 3);
    EXPECT_TRUE(batch->columns.count("id") > 0);
    EXPECT_TRUE(batch->columns.count("age") > 0);

    auto next = scan->next();
    EXPECT_FALSE(next.has_value());
}

// ── Test 18: Slot alignment ────────────────────────────────────────────────
TEST_F(StorageTest, SlotAlignment) {
    // Verify that slot i in column A and slot i in column B correspond to same row.
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    auto scan = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto batch = scan->next();
    ASSERT_TRUE(batch.has_value());

    auto idVals  = ctx->decrypt(*batch->columns["id"].ciphertexts[0], 3).values;
    auto ageVals = ctx->decrypt(*batch->columns["age"].ciphertexts[0], 3).values;
    auto salVals = ctx->decrypt(*batch->columns["salary"].ciphertexts[0], 3).values;

    // Row 0: id=1, age=2, salary=5
    EXPECT_EQ(idVals[0], 1);
    EXPECT_EQ(ageVals[0], 2);
    EXPECT_EQ(salVals[0], 5);
    // Row 2: id=3, age=4, salary=7
    EXPECT_EQ(idVals[2], 3);
    EXPECT_EQ(ageVals[2], 4);
    EXPECT_EQ(salVals[2], 7);
}

// ── Test 19: FilterExec ────────────────────────────────────────────────────
TEST_F(StorageTest, FilterExec) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    // Filter: id == 2
    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 2);
    auto lit = ctx->encrypt(litPv);

    auto predicate = Expression::equal(
        Expression::columnRef("id"),
        Expression::encLiteral(lit));

    auto scan   = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto filter = std::make_shared<FilterExec>(scan, predicate, ctx);

    auto batch = filter->next();
    ASSERT_TRUE(batch.has_value());
    ASSERT_NE(batch->selectionMask, nullptr);

    auto mask = ctx->decrypt(*batch->selectionMask, 3).values;
    EXPECT_EQ(mask[0], 0); // id=1 != 2
    EXPECT_EQ(mask[1], 1); // id=2 == 2
    EXPECT_EQ(mask[2], 0); // id=3 != 2
}

// ── Test 20: ProjectExec ──────────────────────────────────────────────────
TEST_F(StorageTest, ProjectExec) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    auto scan    = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto project = std::make_shared<ProjectExec>(
        scan, std::vector<std::string>{"id", "salary"});

    auto batch = project->next();
    ASSERT_TRUE(batch.has_value());
    EXPECT_TRUE(batch->columns.count("id") > 0);
    EXPECT_TRUE(batch->columns.count("salary") > 0);
    EXPECT_EQ(batch->columns.count("age"), 0u);
}

// ── Test 21: Result mask correctness ──────────────────────────────────────
TEST_F(StorageTest, ResultMaskCorrectness) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    // Filter: age > 2 (should match rows with age=3,4 → rows 1,2)
    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 2);
    auto lit = ctx->encrypt(litPv);

    auto predicate = Expression::gt(
        Expression::columnRef("age"),
        Expression::encLiteral(lit));

    auto scan   = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto filter = std::make_shared<FilterExec>(scan, predicate, ctx);

    auto batch = filter->next();
    ASSERT_TRUE(batch.has_value());
    auto mask = ctx->decrypt(*batch->selectionMask, 3).values;
    EXPECT_EQ(mask[0], 0); // age=2 not > 2
    EXPECT_EQ(mask[1], 1); // age=3 > 2
    EXPECT_EQ(mask[2], 1); // age=4 > 2
}

// ── Test 22: Client-side row reconstruction ───────────────────────────────
TEST_F(StorageTest, ClientRowReconstruction) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    // Filter: id == 3
    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 3);
    auto lit = ctx->encrypt(litPv);

    auto predicate = Expression::equal(
        Expression::columnRef("id"),
        Expression::encLiteral(lit));

    auto scan    = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto filter  = std::make_shared<FilterExec>(scan, predicate, ctx);
    auto project = std::make_shared<ProjectExec>(
        filter, std::vector<std::string>{"id", "salary"});

    auto batch = project->next();
    ASSERT_TRUE(batch.has_value());

    QueryClient client(ctx);
    auto rows = client.decryptAndReconstruct(*batch);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values.at("id"), 3);
    EXPECT_EQ(rows[0].values.at("salary"), 7);
}

// ── Test 23: Multi-chunk execution ────────────────────────────────────────
TEST_F(StorageTest, MultiChunkExecution) {
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n"
          << "4,5,8\n"
          << "5,6,9\n";
    }
    auto schema = employeeSchema();
    // chunk size = 2 → 3 chunks
    ingestCSV(csvPath, schema, 2, *ctx, *storage);

    auto chunks = storage->listChunks("employees", "id");
    EXPECT_EQ(chunks.size(), 3u);

    // Filter: id > 3 → should match id=4,5
    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 3);
    auto lit = ctx->encrypt(litPv);

    auto predicate = Expression::gt(
        Expression::columnRef("id"),
        Expression::encLiteral(lit));

    auto scan   = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "salary"}, storage);
    auto filter = std::make_shared<FilterExec>(scan, predicate, ctx);

    QueryClient client(ctx);
    std::vector<Row> allRows;
    EncryptedResultStream stream(filter);
    while (auto batch = stream.next()) {
        auto rows = client.decryptAndReconstruct(*batch);
        allRows.insert(allRows.end(), rows.begin(), rows.end());
    }

    ASSERT_EQ(allRows.size(), 2u);
    std::vector<int64_t> ids;
    for (auto& r : allRows) ids.push_back(r.values.at("id"));
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], 4);
    EXPECT_EQ(ids[1], 5);
}

// ── Test 24: Invalid plan rejection ───────────────────────────────────────
TEST(PlanValidatorTest, InvalidColumnRejected) {
    Catalog catalog;
    catalog.registerTable(employeeSchema());
    PlanValidator validator(catalog);

    auto expr = Expression::equal(
        Expression::columnRef("nonexistent_column"),
        Expression::columnRef("id"));

    EXPECT_THROW(validator.validateExpression(*expr, "employees"),
                 std::runtime_error);
}

TEST(PlanValidatorTest, InvalidTableRejected) {
    Catalog catalog;
    PlanValidator validator(catalog);

    auto expr = Expression::equal(
        Expression::columnRef("id"),
        Expression::columnRef("id"));

    EXPECT_THROW(validator.validateExpression(*expr, "no_such_table"),
                 std::runtime_error);
}

// ── Test 25: Plaintext vs encrypted result equivalence ────────────────────
TEST_F(StorageTest, PlaintextVsEncryptedEquivalence) {
    // Plaintext reference: filter employees where id==2 AND age>2
    // Expected: row {id=2, age=3, salary=6}
    auto csvPath = (tmpDir / "test.csv").string();
    {
        std::ofstream f(csvPath);
        f << "id,age,salary\n"
          << "1,2,5\n"
          << "2,3,6\n"
          << "3,4,7\n";
    }
    auto schema = employeeSchema();
    ingestCSV(csvPath, schema, 10, *ctx, *storage);

    PlaintextVector lit2pv, lit2pv2;
    lit2pv.values.assign(ctx->slotCount(), 2);
    lit2pv2.values.assign(ctx->slotCount(), 2);
    auto lit2a = ctx->encrypt(lit2pv);
    auto lit2b = ctx->encrypt(lit2pv2);

    // id == 2 AND age > 2
    auto predicate = Expression::andExpr(
        Expression::equal(Expression::columnRef("id"),  Expression::encLiteral(lit2a)),
        Expression::gt(Expression::columnRef("age"),    Expression::encLiteral(lit2b)));

    auto scan    = std::make_shared<TableScanExec>(
        "employees", std::vector<std::string>{"id", "age", "salary"}, storage);
    auto filter  = std::make_shared<FilterExec>(scan, predicate, ctx);
    auto project = std::make_shared<ProjectExec>(
        filter, std::vector<std::string>{"id", "age", "salary"});

    auto batch = project->next();
    ASSERT_TRUE(batch.has_value());

    QueryClient client(ctx);
    auto rows = client.decryptAndReconstruct(*batch);

    // Plaintext reference result
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values.at("id"),     2);
    EXPECT_EQ(rows[0].values.at("age"),    3);
    EXPECT_EQ(rows[0].values.at("salary"), 6);
}
