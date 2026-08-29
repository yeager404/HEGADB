#pragma once
// ExpressionExecutor — evaluates an expression tree over an EncryptedBatch.
// Returns an encrypted selection mask (0/1 per slot).
//
// Boolean arithmetic over BFV packed integers:
//   AND(M1, M2) = M1 * M2          (both must be 0 or 1)
//   OR(M1, M2)  = M1 + M2 - M1*M2
//   NOT(M)      = 1 - M
//
// Equality (EQ):
//   We use the identity: a == b  iff  (a - b) == 0.
//   We compute d = a - b, then raise d to the power (p-1) mod p.
//   By Fermat's little theorem, d^(p-1) = 1 if d != 0, and 0^(p-1) = 0.
//   So EQ_mask = 1 - d^(p-1).
//   This requires multiplicative depth = ceil(log2(p-1)).
//   For p=65537, p-1=65536=2^16, so depth=16 — too deep for practical use.
//
//   PRACTICAL APPROACH for Prototype 1:
//   We use a small plaintext modulus (p=257, prime) so p-1=256=2^8, depth=8.
//   This is still expensive. For the demo we use p=17 (p-1=16=2^4, depth=4).
//   The scheme is correct for values in [0, p/2).
//
//   For GT/LT we use the sign of (a - b):
//   In BFV mod p, a - b > 0 (in the "positive" half [1, (p-1)/2]) means a > b.
//   We compute the indicator using polynomial evaluation.
//   For Prototype 1 we implement GT via the Fermat-based approach on the
//   difference, combined with the sign convention.
//
//   IMPORTANT: All values must be in [0, (p-1)/2] for GT/LT to be meaningful.
//   Document this constraint clearly.

#include "proto1/expression.h"
#include "proto1/encrypted_batch.h"
#include <memory>

namespace proto1 {

class ExpressionExecutor {
public:
    explicit ExpressionExecutor(std::shared_ptr<FHEKeyContext> ctx);

    // Evaluate expression over batch, return encrypted mask (0/1 per slot).
    std::shared_ptr<EncryptedVector>
    evaluate(const Expression& expr, const EncryptedBatch& batch) const;

private:
    std::shared_ptr<FHEKeyContext> ctx_;

    // Resolve a leaf to an EncryptedVector (column or literal).
    std::shared_ptr<EncryptedVector>
    resolveLeaf(const Expression& expr, const EncryptedBatch& batch) const;

    // Compute equality mask: 1 where a==b, 0 elsewhere.
    std::shared_ptr<EncryptedVector>
    evalEqual(const EncryptedVector& a, const EncryptedVector& b) const;

    // Compute GT mask: 1 where a>b, 0 elsewhere.
    std::shared_ptr<EncryptedVector>
    evalGT(const EncryptedVector& a, const EncryptedVector& b) const;

    // Compute LT mask: 1 where a<b, 0 elsewhere.
    std::shared_ptr<EncryptedVector>
    evalLT(const EncryptedVector& a, const EncryptedVector& b) const;

    // Raise each slot to power k using repeated squaring.
    std::shared_ptr<EncryptedVector>
    evalPow(const EncryptedVector& a, uint64_t k) const;
};

} // namespace proto1
