#pragma once
// Physical operator interface and concrete operators.

#include "proto1/encrypted_batch.h"
#include "proto1/expression.h"
#include "proto1/storage.h"
#include "proto1/expression_executor.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace proto1 {

// Base physical operator — pull-based iterator model.
class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;

    // Returns the next batch, or nullopt when exhausted.
    virtual std::optional<EncryptedBatch> next() = 0;

    // Reset to beginning (for re-execution).
    virtual void reset() = 0;
};

// ── TableScanExec ──────────────────────────────────────────────────────────
// Reads encrypted column chunks from storage, chunk by chunk.
// Only loads the requested columns (projection pushdown).
class TableScanExec : public PhysicalOperator {
public:
    TableScanExec(std::string tableId,
                  std::vector<std::string> requiredColumns,
                  std::shared_ptr<EncryptedStorage> storage);

    std::optional<EncryptedBatch> next() override;
    void reset() override;

private:
    std::string              tableId_;
    std::vector<std::string> requiredColumns_;
    std::shared_ptr<EncryptedStorage> storage_;

    // Chunk iteration state
    std::vector<ChunkMeta> chunkList_; // from first required column
    size_t                 chunkIdx_{0};
    bool                   initialized_{false};

    void init();
};

// ── FilterExec ────────────────────────────────────────────────────────────
// Evaluates a predicate expression and attaches the mask to each batch.
class FilterExec : public PhysicalOperator {
public:
    FilterExec(std::shared_ptr<PhysicalOperator> child,
               std::shared_ptr<Expression>       predicate,
               std::shared_ptr<FHEKeyContext>    ctx);

    std::optional<EncryptedBatch> next() override;
    void reset() override;

private:
    std::shared_ptr<PhysicalOperator> child_;
    std::shared_ptr<Expression>       predicate_;
    ExpressionExecutor                executor_;
};

// ── ProjectExec ───────────────────────────────────────────────────────────
// Selects a subset of columns from each batch.
class ProjectExec : public PhysicalOperator {
public:
    ProjectExec(std::shared_ptr<PhysicalOperator> child,
                std::vector<std::string>           outputColumns);

    std::optional<EncryptedBatch> next() override;
    void reset() override;

private:
    std::shared_ptr<PhysicalOperator> child_;
    std::vector<std::string>          outputColumns_;
};

// ── BitmaskFilterExec ──────────────────────────────────────────────────────
// Evaluates a predicate and returns ONLY the encrypted bitmask (no columns).
// Used for client-side decryption of bitmask, then server-side filtering.
class BitmaskFilterExec : public PhysicalOperator {
public:
    BitmaskFilterExec(std::shared_ptr<PhysicalOperator> child,
                      std::shared_ptr<Expression>       predicate,
                      std::shared_ptr<FHEKeyContext>    ctx);

    std::optional<EncryptedBatch> next() override;
    void reset() override;

private:
    std::shared_ptr<PhysicalOperator> child_;
    std::shared_ptr<Expression>       predicate_;
    ExpressionExecutor                executor_;
};

} // namespace proto1
