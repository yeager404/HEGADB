// OpenFHE BFV implementation of FHEKeyContext / ClientKeyContext.
//
// Scheme: BFV — exact integer arithmetic mod p.
// CryptoContext<DCRTPoly> is already a shared_ptr<CryptoContextImpl<...>>.
// Serial::Serialize / Deserialize return void (not bool).
// Serial::SerializeToFile / DeserializeFromFile return bool.

#include "proto1/fhe_runtime.h"
#include "proto1/logger.h"

#include "openfhe.h"
#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#include "scheme/bfvrns/bfvrns-ser.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace proto1 {

using namespace lbcrypto;
namespace fs = std::filesystem;

// ── BFVEncryptedVector ─────────────────────────────────────────────────────

class BFVEncryptedVector : public EncryptedVector {
public:
    BFVEncryptedVector(Ciphertext<DCRTPoly> ct,
                       size_t logicalSize,
                       size_t slotCount,
                       CryptoContext<DCRTPoly> cc)
        : ct_(std::move(ct))
        , logicalSize_(logicalSize)
        , slotCount_(slotCount)
        , cc_(cc) {}

    size_t logicalSize() const override { return logicalSize_; }
    size_t slotCount()   const override { return slotCount_; }

    std::vector<uint8_t> serialize() const override {
        std::ostringstream oss(std::ios::binary);
        Serial::Serialize(ct_, oss, SerType::BINARY);
        auto s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    const Ciphertext<DCRTPoly>& ciphertext() const { return ct_; }
    Ciphertext<DCRTPoly>&       ciphertext()       { return ct_; }

private:
    Ciphertext<DCRTPoly> ct_;
    size_t               logicalSize_;
    size_t               slotCount_;
    CryptoContext<DCRTPoly> cc_; // shared_ptr under the hood — cheap to copy
};

// ── BFVKeyContext ──────────────────────────────────────────────────────────

class BFVKeyContext : public ClientKeyContext {
public:
    explicit BFVKeyContext(const FHEParameterSet& params) : paramSetId_(params.id) {
        CCParams<CryptoContextBFVRNS> ccParams;
        ccParams.SetPlaintextModulus(params.plaintextModulus);
        ccParams.SetMultiplicativeDepth(params.multiplicativeDepth);
        if (params.ringDimension > 0)
            ccParams.SetRingDim(params.ringDimension);
        // Disable security enforcement for Prototype 1 (research/testing only).
        // Production deployments must use HEStd_128_classic or higher.
        ccParams.SetSecurityLevel(HEStd_NotSet);

        cc_ = GenCryptoContext(ccParams);
        cc_->Enable(PKE);
        cc_->Enable(KEYSWITCH);
        cc_->Enable(LEVELEDSHE);

        slotCount_ = cc_->GetRingDimension() / 2;
        LOG_INFO("BFVKeyContext", "Created: paramSetId=" + paramSetId_ +
                 " slotCount=" + std::to_string(slotCount_) +
                 " p=" + std::to_string(params.plaintextModulus) +
                 " depth=" + std::to_string(params.multiplicativeDepth));
    }

    BFVKeyContext() = default;

    void generateKeys() {
        keyPair_ = cc_->KeyGen();
        cc_->EvalMultKeyGen(keyPair_.secretKey);
        LOG_INFO("BFVKeyContext", "Keys generated");
    }

    const std::string& paramSetId() const override { return paramSetId_; }
    size_t             slotCount()  const override { return slotCount_; }

    std::shared_ptr<EncryptedVector>
    encrypt(const PlaintextVector& v) const override {
        if (!keyPair_.publicKey)
            throw std::runtime_error("BFVKeyContext: no public key");
        auto pt = cc_->MakePackedPlaintext(v.values);
        auto ct = cc_->Encrypt(keyPair_.publicKey, pt);
        return std::make_shared<BFVEncryptedVector>(std::move(ct), v.values.size(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalAdd(const EncryptedVector& a, const EncryptedVector& b) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        auto& cb = dynamic_cast<const BFVEncryptedVector&>(b).ciphertext();
        return std::make_shared<BFVEncryptedVector>(cc_->EvalAdd(ca, cb), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalMult(const EncryptedVector& a, const EncryptedVector& b) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        auto& cb = dynamic_cast<const BFVEncryptedVector&>(b).ciphertext();
        return std::make_shared<BFVEncryptedVector>(cc_->EvalMult(ca, cb), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalSub(const EncryptedVector& a, const EncryptedVector& b) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        auto& cb = dynamic_cast<const BFVEncryptedVector&>(b).ciphertext();
        return std::make_shared<BFVEncryptedVector>(cc_->EvalSub(ca, cb), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalAddPlain(const EncryptedVector& a, int64_t scalar) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        std::vector<int64_t> sv(slotCount_, scalar);
        auto pt = cc_->MakePackedPlaintext(sv);
        return std::make_shared<BFVEncryptedVector>(cc_->EvalAdd(ca, pt), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalMultPlain(const EncryptedVector& a, int64_t scalar) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        std::vector<int64_t> sv(slotCount_, scalar);
        auto pt = cc_->MakePackedPlaintext(sv);
        return std::make_shared<BFVEncryptedVector>(cc_->EvalMult(ca, pt), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    evalNegate(const EncryptedVector& a) const override {
        auto& ca = dynamic_cast<const BFVEncryptedVector&>(a).ciphertext();
        return std::make_shared<BFVEncryptedVector>(cc_->EvalNegate(ca), a.logicalSize(), slotCount_, cc_);
    }

    std::shared_ptr<EncryptedVector>
    deserialize(const std::vector<uint8_t>& bytes) const override {
        return deserializeWithSize(bytes, 0);
    }

    std::shared_ptr<EncryptedVector>
    deserializeWithSize(const std::vector<uint8_t>& bytes, size_t logicalSize) const override {
        std::string s(bytes.begin(), bytes.end());
        std::istringstream iss(s, std::ios::binary);
        Ciphertext<DCRTPoly> ct;
        Serial::Deserialize(ct, iss, SerType::BINARY);
        if (!ct)
            throw std::runtime_error("BFVKeyContext: ciphertext deserialization produced null");
        return std::make_shared<BFVEncryptedVector>(std::move(ct), logicalSize, slotCount_, cc_);
    }

    PlaintextVector
    decrypt(const EncryptedVector& v, size_t logicalLen) const override {
        if (!keyPair_.secretKey)
            throw std::runtime_error("BFVKeyContext: no secret key (client only)");
        auto& cv = dynamic_cast<const BFVEncryptedVector&>(v).ciphertext();
        Plaintext pt;
        cc_->Decrypt(keyPair_.secretKey, cv, &pt);
        pt->SetLength(logicalLen);
        auto packed = pt->GetPackedValue();
        PlaintextVector result;
        result.values.assign(packed.begin(), packed.begin() + logicalLen);
        return result;
    }

    void saveServerKeys(const std::string& dir) const override {
        fs::create_directories(dir);
        if (!Serial::SerializeToFile(dir + "/cryptocontext.bin", cc_, SerType::BINARY))
            throw std::runtime_error("Failed to save crypto context");
        if (!Serial::SerializeToFile(dir + "/key-public.bin", keyPair_.publicKey, SerType::BINARY))
            throw std::runtime_error("Failed to save public key");
        std::ofstream emk(dir + "/key-eval-mult.bin", std::ios::binary);
        if (!cc_->SerializeEvalMultKey(emk, SerType::BINARY))
            throw std::runtime_error("Failed to save eval mult keys");
        std::ofstream pid(dir + "/param-set-id.txt");
        pid << paramSetId_;
        LOG_INFO("BFVKeyContext", "Server keys saved to " + dir);
    }

    void loadServerKeys(const std::string& dir) override {
        // Clear global OpenFHE context registry to avoid key tag conflicts
        // when loading multiple contexts in the same process (e.g., tests).
        CryptoContextFactory<DCRTPoly>::ReleaseAllContexts();
        if (!Serial::DeserializeFromFile(dir + "/cryptocontext.bin", cc_, SerType::BINARY))
            throw std::runtime_error("Failed to load crypto context from " + dir);
        cc_->Enable(PKE);
        cc_->Enable(KEYSWITCH);
        cc_->Enable(LEVELEDSHE);
        slotCount_ = cc_->GetRingDimension() / 2;
        if (!Serial::DeserializeFromFile(dir + "/key-public.bin", keyPair_.publicKey, SerType::BINARY))
            throw std::runtime_error("Failed to load public key");
        std::ifstream emk(dir + "/key-eval-mult.bin", std::ios::binary);
        if (!emk.is_open())
            throw std::runtime_error("Cannot open eval mult key file: " + dir + "/key-eval-mult.bin");
        if (!cc_->DeserializeEvalMultKey(emk, SerType::BINARY))
            throw std::runtime_error("Failed to load eval mult keys");
        std::ifstream pid(dir + "/param-set-id.txt");
        std::getline(pid, paramSetId_);
        LOG_INFO("BFVKeyContext", "Server keys loaded from " + dir);
    }

    void saveCryptoContext(const std::string& dir) const override { saveServerKeys(dir); }
    void loadCryptoContext(const std::string& dir) override       { loadServerKeys(dir); }

    void savePrivateKey(const std::string& dir) const override {
        fs::create_directories(dir);
        if (!Serial::SerializeToFile(dir + "/key-private.bin", keyPair_.secretKey, SerType::BINARY))
            throw std::runtime_error("Failed to save private key");
        LOG_INFO("BFVKeyContext", "Private key saved to " + dir);
    }

    void loadPrivateKey(const std::string& dir) override {
        if (!Serial::DeserializeFromFile(dir + "/key-private.bin", keyPair_.secretKey, SerType::BINARY))
            throw std::runtime_error("Failed to load private key from " + dir);
        LOG_INFO("BFVKeyContext", "Private key loaded from " + dir);
    }

    CryptoContext<DCRTPoly> cryptoContext() const { return cc_; }

private:
    CryptoContext<DCRTPoly> cc_;   // already a shared_ptr<CryptoContextImpl<...>>
    KeyPair<DCRTPoly>       keyPair_;
    std::string             paramSetId_;
    size_t                  slotCount_{0};
};

// ── Factory implementations ────────────────────────────────────────────────

std::shared_ptr<ClientKeyContext>
createBFVContext(const FHEParameterSet& params) {
    auto ctx = std::make_shared<BFVKeyContext>(params);
    ctx->generateKeys();
    return ctx;
}

std::shared_ptr<FHEKeyContext>
loadServerContext(const std::string& dir) {
    auto ctx = std::make_shared<BFVKeyContext>();
    ctx->loadServerKeys(dir);
    return ctx;
}

std::shared_ptr<ClientKeyContext>
loadClientContext(const std::string& dir) {
    auto ctx = std::make_shared<BFVKeyContext>();
    ctx->loadServerKeys(dir);
    ctx->loadPrivateKey(dir + "/client");
    return ctx;
}

} // namespace proto1
