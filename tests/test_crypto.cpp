// Unit tests for the crypto layer.
// Tests: key generation, encrypt/decrypt, packed vectors, serialization.

#include <gtest/gtest.h>
#include "proto1/fhe_runtime.h"
#include <filesystem>

using namespace proto1;
namespace fs = std::filesystem;

static FHEParameterSet testParams() {
    FHEParameterSet p;
    p.id                  = "bfv-257-depth8";
    p.plaintextModulus    = 257;
    p.multiplicativeDepth = 8;
    p.ringDimension       = 128;
    return p;
}

// ── Test 1: Key generation ─────────────────────────────────────────────────
TEST(CryptoTest, KeyGeneration) {
    auto ctx = createBFVContext(testParams());
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->paramSetId(), "bfv-257-depth8");
    EXPECT_GT(ctx->slotCount(), 0u);
}

// ── Test 2: Encrypt / Decrypt ──────────────────────────────────────────────
TEST(CryptoTest, EncryptDecrypt) {
    auto ctx = createBFVContext(testParams());
    PlaintextVector pv;
    pv.values = {1, 2, 3, 4, 5};

    auto ct = ctx->encrypt(pv);
    ASSERT_NE(ct, nullptr);
    EXPECT_EQ(ct->logicalSize(), 5u);

    auto result = ctx->decrypt(*ct, 5);
    ASSERT_EQ(result.values.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(result.values[i], pv.values[i]) << "mismatch at slot " << i;
}

// ── Test 3: Packed vector encryption ──────────────────────────────────────
TEST(CryptoTest, PackedVectorEncryption) {
    auto ctx = createBFVContext(testParams());
    size_t slots = ctx->slotCount();
    EXPECT_GT(slots, 0u);

    PlaintextVector pv;
    pv.values.resize(slots, 0);
    for (size_t i = 0; i < slots; ++i)
        pv.values[i] = static_cast<int64_t>(i % 8); // keep values < p/2

    auto ct = ctx->encrypt(pv);
    auto result = ctx->decrypt(*ct, slots);
    ASSERT_EQ(result.values.size(), slots);
    for (size_t i = 0; i < slots; ++i)
        EXPECT_EQ(result.values[i], pv.values[i]) << "slot " << i;
}

// ── Test 4: Homomorphic addition ───────────────────────────────────────────
TEST(CryptoTest, HomomorphicAdd) {
    auto ctx = createBFVContext(testParams());
    PlaintextVector a, b;
    a.values = {1, 2, 3};
    b.values = {4, 5, 6};

    auto ca = ctx->encrypt(a);
    auto cb = ctx->encrypt(b);
    auto csum = ctx->evalAdd(*ca, *cb);
    auto result = ctx->decrypt(*csum, 3);

    EXPECT_EQ(result.values[0], 5);
    EXPECT_EQ(result.values[1], 7);
    EXPECT_EQ(result.values[2], 9);
}

// ── Test 5: Homomorphic multiplication ────────────────────────────────────
TEST(CryptoTest, HomomorphicMult) {
    auto ctx = createBFVContext(testParams());
    PlaintextVector a, b;
    a.values = {2, 3, 4};
    b.values = {3, 2, 1};

    auto ca = ctx->encrypt(a);
    auto cb = ctx->encrypt(b);
    auto cprod = ctx->evalMult(*ca, *cb);
    auto result = ctx->decrypt(*cprod, 3);

    EXPECT_EQ(result.values[0], 6);
    EXPECT_EQ(result.values[1], 6);
    EXPECT_EQ(result.values[2], 4);
}

// ── Test 6: Ciphertext serialization / deserialization ────────────────────
TEST(CryptoTest, SerializeDeserialize) {
    auto ctx = createBFVContext(testParams());
    PlaintextVector pv;
    pv.values = {3, 1, 4, 1, 5};

    auto ct = ctx->encrypt(pv);
    auto bytes = ct->serialize();
    EXPECT_GT(bytes.size(), 0u);

    auto ct2 = ctx->deserializeWithSize(bytes, 5);
    ASSERT_NE(ct2, nullptr);

    auto result = ctx->decrypt(*ct2, 5);
    ASSERT_EQ(result.values.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(result.values[i], pv.values[i]);
}

// ── Test 7: Key persistence (server keys) ─────────────────────────────────
TEST(CryptoTest, KeyPersistence) {
    auto tmpDir = fs::temp_directory_path() / "proto1_test_keys";
    fs::remove_all(tmpDir);

    auto ctx = createBFVContext(testParams());
    ctx->saveServerKeys(tmpDir.string());
    ctx->savePrivateKey((tmpDir / "client").string());

    // Load server context (no private key)
    auto serverCtx = loadServerContext(tmpDir.string());
    ASSERT_NE(serverCtx, nullptr);
    EXPECT_EQ(serverCtx->paramSetId(), "bfv-257-depth8");

    // Load client context (with private key)
    auto clientCtx = loadClientContext(tmpDir.string());
    ASSERT_NE(clientCtx, nullptr);

    // Encrypt with original, decrypt with loaded client
    PlaintextVector pv;
    pv.values = {7, 3, 1};
    auto ct = ctx->encrypt(pv);
    auto bytes = ct->serialize();
    auto ct2 = clientCtx->deserializeWithSize(bytes, 3);
    auto result = clientCtx->decrypt(*ct2, 3);
    EXPECT_EQ(result.values[0], 7);
    EXPECT_EQ(result.values[1], 3);
    EXPECT_EQ(result.values[2], 1);

    fs::remove_all(tmpDir);
}
