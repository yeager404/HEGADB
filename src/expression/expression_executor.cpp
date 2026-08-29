// ExpressionExecutor implementation.
//
// Equality: EQ(a,b) = 1 - (a-b)^(p-1)
//   Requires depth = ceil(log2(p-1)).
//   For p=17: depth=4. For p=257: depth=8.
//
// GT(a,b): a > b in Z_p means (a-b) is in {1,...,(p-1)/2}.
//   We compute: GT = sum_{k=1}^{(p-1)/2} EQ(a-b, k)
//   = sum_{k=1}^{(p-1)/2} [1 - (a-b-k)^(p-1)]
//   = (p-1)/2 - sum_{k=1}^{(p-1)/2} (a-b-k)^(p-1)
//   This is O(p) multiplications — too expensive for large p.
//
//   BETTER: Use the Lagrange interpolation polynomial L(x) over Z_p where
//   L(k) = 1 for k in {1,...,(p-1)/2} and L(k) = 0 for k in {0,(p+1)/2,...,p-1}.
//   Evaluate L(a-b) homomorphically.
//   For p=17: L has degree 16, evaluable in depth 4 with baby-step/giant-step.
//
//   For Prototype 1 we implement the direct sum approach for small p=17,
//   and document the polynomial approach for larger p.
//
// LT(a,b) = GT(b,a)
//
// AND(M1,M2) = M1 * M2
// OR(M1,M2)  = M1 + M2 - M1*M2
// NOT(M)     = 1 - M

#include "proto1/expression_executor.h"
#include "proto1/logger.h"

#include <stdexcept>

namespace proto1 {

ExpressionExecutor::ExpressionExecutor(std::shared_ptr<FHEKeyContext> ctx)
    : ctx_(std::move(ctx)) {}

std::shared_ptr<EncryptedVector>
ExpressionExecutor::resolveLeaf(const Expression& expr,
                                const EncryptedBatch& batch) const {
    if (expr.kind == ExprKind::ColumnRef) {
        auto it = batch.columns.find(expr.columnName);
        if (it == batch.columns.end())
            throw std::runtime_error("Column not found in batch: " + expr.columnName);
        if (it->second.ciphertexts.empty())
            throw std::runtime_error("Column has no ciphertexts: " + expr.columnName);
        // For now, each column chunk has exactly one ciphertext per chunk.
        return it->second.ciphertexts[0];
    }
    if (expr.kind == ExprKind::EncryptedLiteral) {
        if (!expr.literal)
            throw std::runtime_error("EncryptedLiteral has null literal");
        return expr.literal;
    }
    throw std::runtime_error("resolveLeaf called on non-leaf expression");
}

// Raise each slot to power k using repeated squaring.
// Depth = ceil(log2(k)).
std::shared_ptr<EncryptedVector>
ExpressionExecutor::evalPow(const EncryptedVector& a, uint64_t k) const {
    // Use slotCount for padding — logicalSize may be 0 for deserialized literals.
    size_t sz = ctx_->slotCount();

    if (k == 0) {
        PlaintextVector ones;
        ones.values.assign(sz, 1);
        return ctx_->encrypt(ones);
    }
    if (k == 1) {
        PlaintextVector zeros;
        zeros.values.assign(sz, 0);
        auto zero = ctx_->encrypt(zeros);
        return ctx_->evalAdd(a, *zero);
    }

    std::shared_ptr<EncryptedVector> result;
    std::shared_ptr<EncryptedVector> base;
    {
        PlaintextVector zeros;
        zeros.values.assign(sz, 0);
        auto zero = ctx_->encrypt(zeros);
        base = ctx_->evalAdd(a, *zero);
    }

    uint64_t exp = k;
    while (exp > 0) {
        if (exp & 1) {
            if (!result) {
                result = base;
            } else {
                result = ctx_->evalMult(*result, *base);
            }
        }
        exp >>= 1;
        if (exp > 0)
            base = ctx_->evalMult(*base, *base);
    }
    return result;
}

// EQ(a,b) = 1 - (a-b)^(p-1)
// The plaintext modulus p must be known. We retrieve it from the context
// via a convention: the paramSetId encodes it as "bfv-p-depthD".
// For simplicity, we store p as a member set during construction.
// We use a static helper to parse it from paramSetId.
static uint64_t parsePlaintextModulus(const std::string& paramSetId) {
    // Format: "bfv-<p>-depth<d>"
    auto pos1 = paramSetId.find('-');
    if (pos1 == std::string::npos) return 65537;
    auto pos2 = paramSetId.find('-', pos1 + 1);
    if (pos2 == std::string::npos) return 65537;
    return std::stoull(paramSetId.substr(pos1 + 1, pos2 - pos1 - 1));
}

std::shared_ptr<EncryptedVector>
ExpressionExecutor::evalEqual(const EncryptedVector& a,
                               const EncryptedVector& b) const {
    LOG_DEBUG("ExprExec", "evalEqual");
    uint64_t p = parsePlaintextModulus(ctx_->paramSetId());
    uint64_t pm1 = p - 1;

    // d = a - b
    auto d = ctx_->evalSub(a, b);

    // d^(p-1)
    auto dpow = evalPow(*d, pm1);

    // mask = 1 - d^(p-1)
    // evalAddPlain(x, c) adds c to every slot.
    // We need: 1 - dpow = -dpow + 1 = evalNegate(dpow) + 1
    auto neg = ctx_->evalNegate(*dpow);
    auto mask = ctx_->evalAddPlain(*neg, 1);
    return mask;
}

std::shared_ptr<EncryptedVector>
ExpressionExecutor::evalGT(const EncryptedVector& a,
                            const EncryptedVector& b) const {
    LOG_DEBUG("ExprExec", "evalGT");
    // GT(a,b) = sum_{k=1}^{(p-1)/2} EQ(a-b, k)
    // = sum_{k=1}^{(p-1)/2} [1 - (a-b-k)^(p-1)]
    // = (p-1)/2 - sum_{k=1}^{(p-1)/2} (a-b-k)^(p-1)
    //
    // For p=17: sum over k=1..8 (8 terms, each depth 4) → total depth 4.
    // For p=257: sum over k=1..128 (128 terms) → expensive but correct.
    // We implement this directly.

    uint64_t p = parsePlaintextModulus(ctx_->paramSetId());
    uint64_t pm1 = p - 1;
    uint64_t half = (p - 1) / 2;

    // d = a - b
    auto d = ctx_->evalSub(a, b);

    // Accumulate sum of (d - k)^(p-1) for k = 1..half
    std::shared_ptr<EncryptedVector> sumPow;

    for (uint64_t k = 1; k <= half; ++k) {
        // dk = d - k  (add -k = p-k in Z_p, but evalAddPlain handles negatives)
        auto dk = ctx_->evalAddPlain(*d, -(int64_t)k);
        auto dkpow = evalPow(*dk, pm1);
        if (!sumPow) {
            sumPow = dkpow;
        } else {
            sumPow = ctx_->evalAdd(*sumPow, *dkpow);
        }
    }

    // GT = half - sumPow
    // = evalNegate(sumPow) + half
    auto neg = ctx_->evalNegate(*sumPow);
    auto gt = ctx_->evalAddPlain(*neg, (int64_t)half);
    return gt;
}

std::shared_ptr<EncryptedVector>
ExpressionExecutor::evalLT(const EncryptedVector& a,
                            const EncryptedVector& b) const {
    LOG_DEBUG("ExprExec", "evalLT");
    // LT(a,b) = GT(b,a)
    return evalGT(b, a);
}

std::shared_ptr<EncryptedVector>
ExpressionExecutor::evaluate(const Expression& expr,
                              const EncryptedBatch& batch) const {
    switch (expr.kind) {
        case ExprKind::ColumnRef:
        case ExprKind::EncryptedLiteral:
            // Leaf nodes return the raw encrypted value, not a mask.
            // This should only be called as part of a comparison.
            throw std::runtime_error("Cannot evaluate a leaf expression as a mask");

        case ExprKind::Equal: {
            auto lv = resolveLeaf(*expr.left, batch);
            auto rv = resolveLeaf(*expr.right, batch);
            return evalEqual(*lv, *rv);
        }

        case ExprKind::GreaterThan: {
            auto lv = resolveLeaf(*expr.left, batch);
            auto rv = resolveLeaf(*expr.right, batch);
            return evalGT(*lv, *rv);
        }

        case ExprKind::LessThan: {
            auto lv = resolveLeaf(*expr.left, batch);
            auto rv = resolveLeaf(*expr.right, batch);
            return evalLT(*lv, *rv);
        }

        case ExprKind::And: {
            auto m1 = evaluate(*expr.left, batch);
            auto m2 = evaluate(*expr.right, batch);
            // AND = M1 * M2
            return ctx_->evalMult(*m1, *m2);
        }

        case ExprKind::Or: {
            auto m1 = evaluate(*expr.left, batch);
            auto m2 = evaluate(*expr.right, batch);
            // OR = M1 + M2 - M1*M2
            auto m1m2 = ctx_->evalMult(*m1, *m2);
            auto sum  = ctx_->evalAdd(*m1, *m2);
            return ctx_->evalSub(*sum, *m1m2);
        }

        case ExprKind::Not: {
            auto m = evaluate(*expr.left, batch);
            // NOT = 1 - M
            auto neg = ctx_->evalNegate(*m);
            return ctx_->evalAddPlain(*neg, 1);
        }
    }
    throw std::runtime_error("Unknown expression kind");
}

} // namespace proto1
