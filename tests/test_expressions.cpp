// Tests for expression evaluation: equality, GT, LT, AND, OR, NOT.
// Uses p=17 so values must be in [0, 8].

#include <gtest/gtest.h>
#include "proto1/fhe_runtime.h"
#include "proto1/expression.h"
#include "proto1/expression_executor.h"
#include "proto1/encrypted_batch.h"

using namespace proto1;

static FHEParameterSet testParams() {
    FHEParameterSet p;
    p.id                  = "bfv-257-depth8";
    p.plaintextModulus    = 257;
    p.multiplicativeDepth = 8;
    p.ringDimension       = 128;
    return p;
}

// Helper: encrypt a vector of values into a batch column.
static EncryptedBatch makeBatch(std::shared_ptr<FHEKeyContext> ctx,
                                 const std::string& colName,
                                 const std::vector<int64_t>& vals) {
    PlaintextVector pv;
    pv.values = vals;
    pv.values.resize(ctx->slotCount(), 0);
    auto ct = ctx->encrypt(pv);

    EncryptedColumnChunk chunk;
    chunk.meta.tableId         = "t";
    chunk.meta.columnId        = colName;
    chunk.meta.chunkId         = 0;
    chunk.meta.logicalRowCount = static_cast<int64_t>(vals.size());
    chunk.meta.ciphertextCount = 1;
    chunk.ciphertexts.push_back(ct);

    EncryptedBatch batch;
    batch.chunkId         = 0;
    batch.logicalRowCount = static_cast<int64_t>(vals.size());
    batch.columns[colName] = std::move(chunk);
    return batch;
}

// Helper: decrypt mask and return as vector.
static std::vector<int64_t> decryptMask(std::shared_ptr<ClientKeyContext> ctx,
                                         const EncryptedVector& mask,
                                         size_t len) {
    auto pv = ctx->decrypt(mask, len);
    return pv.values;
}

class ExprTest : public ::testing::Test {
protected:
    std::shared_ptr<ClientKeyContext> ctx;
    void SetUp() override { ctx = createBFVContext(testParams()); }
};

// ── Test 8: Equality ───────────────────────────────────────────────────────
TEST_F(ExprTest, Equality) {
    // col = [1, 2, 3, 4, 5], literal = 3
    // expected mask = [0, 0, 1, 0, 0]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    auto litEnc = ctx->encrypt(PlaintextVector{{3, 3, 3, 3, 3}});
    // Pad literal to slot count
    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 3);
    litEnc = ctx->encrypt(litPv);

    auto expr = Expression::equal(
        Expression::columnRef("col"),
        Expression::encLiteral(litEnc));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 0);
    EXPECT_EQ(result[4], 0);
}

// ── Test 9: Greater-than ───────────────────────────────────────────────────
TEST_F(ExprTest, GreaterThan) {
    // col = [1, 2, 3, 4, 5], literal = 3
    // expected mask = [0, 0, 0, 1, 1]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 3);
    auto litEnc = ctx->encrypt(litPv);

    auto expr = Expression::gt(
        Expression::columnRef("col"),
        Expression::encLiteral(litEnc));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 0);
    EXPECT_EQ(result[3], 1);
    EXPECT_EQ(result[4], 1);
}

// ── Test 10: Less-than ─────────────────────────────────────────────────────
TEST_F(ExprTest, LessThan) {
    // col = [1, 2, 3, 4, 5], literal = 3
    // expected mask = [1, 1, 0, 0, 0]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    PlaintextVector litPv;
    litPv.values.assign(ctx->slotCount(), 3);
    auto litEnc = ctx->encrypt(litPv);

    auto expr = Expression::lt(
        Expression::columnRef("col"),
        Expression::encLiteral(litEnc));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 1);
    EXPECT_EQ(result[2], 0);
    EXPECT_EQ(result[3], 0);
    EXPECT_EQ(result[4], 0);
}

// ── Test 11: AND ───────────────────────────────────────────────────────────
TEST_F(ExprTest, AndExpression) {
    // col = [1,2,3,4,5], EQ(col,3) AND GT(col,2) = [0,0,1,0,0]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    PlaintextVector lit3pv;
    lit3pv.values.assign(ctx->slotCount(), 3);
    auto lit3 = ctx->encrypt(lit3pv);

    PlaintextVector lit2pv;
    lit2pv.values.assign(ctx->slotCount(), 2);
    auto lit2 = ctx->encrypt(lit2pv);

    auto expr = Expression::andExpr(
        Expression::equal(Expression::columnRef("col"), Expression::encLiteral(lit3)),
        Expression::gt(Expression::columnRef("col"), Expression::encLiteral(lit2)));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 0);
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 0);
    EXPECT_EQ(result[4], 0);
}

// ── Test 12: OR ────────────────────────────────────────────────────────────
TEST_F(ExprTest, OrExpression) {
    // col = [1,2,3,4,5], EQ(col,1) OR EQ(col,5) = [1,0,0,0,1]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    PlaintextVector lit1pv, lit5pv;
    lit1pv.values.assign(ctx->slotCount(), 1);
    lit5pv.values.assign(ctx->slotCount(), 5);
    auto lit1 = ctx->encrypt(lit1pv);
    auto lit5 = ctx->encrypt(lit5pv);

    auto expr = Expression::orExpr(
        Expression::equal(Expression::columnRef("col"), Expression::encLiteral(lit1)),
        Expression::equal(Expression::columnRef("col"), Expression::encLiteral(lit5)));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 0);
    EXPECT_EQ(result[3], 0);
    EXPECT_EQ(result[4], 1);
}

// ── Test 13: NOT ───────────────────────────────────────────────────────────
TEST_F(ExprTest, NotExpression) {
    // col = [1,2,3,4,5], NOT(EQ(col,3)) = [1,1,0,1,1]
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    auto batch = makeBatch(ctx, "col", vals);

    PlaintextVector lit3pv;
    lit3pv.values.assign(ctx->slotCount(), 3);
    auto lit3 = ctx->encrypt(lit3pv);

    auto expr = Expression::notExpr(
        Expression::equal(Expression::columnRef("col"), Expression::encLiteral(lit3)));

    ExpressionExecutor exec(ctx);
    auto mask = exec.evaluate(*expr, batch);
    auto result = decryptMask(ctx, *mask, vals.size());

    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 1);
    EXPECT_EQ(result[2], 0);
    EXPECT_EQ(result[3], 1);
    EXPECT_EQ(result[4], 1);
}
