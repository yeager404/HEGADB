#pragma once
// FHE Runtime abstraction — hides OpenFHE from the rest of the engine.
// The execution engine only sees these interfaces; OpenFHE types never
// appear outside src/crypto/.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace proto1 {

// Opaque handle to a single packed ciphertext.
// Internally wraps lbcrypto::Ciphertext<DCRTPoly>.
class EncryptedVector {
public:
    virtual ~EncryptedVector() = default;

    // Number of logical slots used in this ciphertext.
    virtual size_t logicalSize() const = 0;

    // Total slot capacity of the underlying ciphertext.
    virtual size_t slotCount() const = 0;

    // Serialize to bytes (uses OpenFHE binary serialization).
    virtual std::vector<uint8_t> serialize() const = 0;
};

// A plaintext vector ready for encryption.
struct PlaintextVector {
    std::vector<int64_t> values;
};

// Parameters that describe an FHE context.
struct FHEParameterSet {
    std::string id;          // e.g. "bfv-65537-depth4"
    uint64_t    plaintextModulus{65537};
    uint32_t    multiplicativeDepth{4};
    uint32_t    ringDimension{0}; // 0 = auto
};

// Owns the crypto context and all evaluation keys.
// The private key is NOT stored here — it lives in ClientKeyContext.
class FHEKeyContext {
public:
    virtual ~FHEKeyContext() = default;

    virtual const std::string& paramSetId() const = 0;
    virtual size_t             slotCount()  const = 0;

    // Encrypt a packed integer vector.
    virtual std::shared_ptr<EncryptedVector>
    encrypt(const PlaintextVector& v) const = 0;

    // Homomorphic operations (server-side, no secret key).
    virtual std::shared_ptr<EncryptedVector>
    evalAdd(const EncryptedVector& a, const EncryptedVector& b) const = 0;

    virtual std::shared_ptr<EncryptedVector>
    evalMult(const EncryptedVector& a, const EncryptedVector& b) const = 0;

    virtual std::shared_ptr<EncryptedVector>
    evalSub(const EncryptedVector& a, const EncryptedVector& b) const = 0;

    // Add a plaintext constant to every slot.
    virtual std::shared_ptr<EncryptedVector>
    evalAddPlain(const EncryptedVector& a, int64_t scalar) const = 0;

    // Multiply every slot by a plaintext constant.
    virtual std::shared_ptr<EncryptedVector>
    evalMultPlain(const EncryptedVector& a, int64_t scalar) const = 0;

    // Negate every slot.
    virtual std::shared_ptr<EncryptedVector>
    evalNegate(const EncryptedVector& a) const = 0;

    // Deserialize a ciphertext from bytes.
    virtual std::shared_ptr<EncryptedVector>
    deserialize(const std::vector<uint8_t>& bytes) const = 0;

    // Deserialize with explicit logical size.
    virtual std::shared_ptr<EncryptedVector>
    deserializeWithSize(const std::vector<uint8_t>& bytes, size_t logicalSize) const = 0;

    // Serialize the public key + eval keys to a directory (server side).
    virtual void saveServerKeys(const std::string& dir) const = 0;

    // Load server-side keys from a directory.
    virtual void loadServerKeys(const std::string& dir) = 0;

    // Save the crypto context (parameters) to a directory.
    virtual void saveCryptoContext(const std::string& dir) const = 0;
    virtual void loadCryptoContext(const std::string& dir) = 0;
};

// Extends FHEKeyContext with decryption — belongs to the trusted client only.
class ClientKeyContext : public FHEKeyContext {
public:
    virtual ~ClientKeyContext() = default;

    // Decrypt a packed ciphertext.
    virtual PlaintextVector
    decrypt(const EncryptedVector& v, size_t logicalLen) const = 0;

    // Save the private key to a client-side directory.
    virtual void savePrivateKey(const std::string& dir) const = 0;
    virtual void loadPrivateKey(const std::string& dir) = 0;
};

// Factory: create a fresh context with new keys.
std::shared_ptr<ClientKeyContext>
createBFVContext(const FHEParameterSet& params);

// Factory: load a server-side context (no private key).
std::shared_ptr<FHEKeyContext>
loadServerContext(const std::string& dir);

// Factory: load a full client context (with private key).
std::shared_ptr<ClientKeyContext>
loadClientContext(const std::string& dir);

} // namespace proto1
