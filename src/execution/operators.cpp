// Physical operator implementations: TableScanExec, FilterExec, ProjectExec.

#include "proto1/operators.h"
#include "proto1/logger.h"
#include <stdexcept>

namespace proto1 {

// ── TableScanExec ──────────────────────────────────────────────────────────

TableScanExec::TableScanExec(std::string tableId,
                              std::vector<std::string> requiredColumns,
                              std::shared_ptr<EncryptedStorage> storage)
    : tableId_(std::move(tableId))
    , requiredColumns_(std::move(requiredColumns))
    , storage_(std::move(storage)) {}

void TableScanExec::init() {
    if (requiredColumns_.empty())
        throw std::runtime_error("TableScanExec: no required columns");
    // Use the first required column to enumerate chunks.
    chunkList_ = storage_->listChunks(tableId_, requiredColumns_[0]);
    chunkIdx_  = 0;
    initialized_ = true;
    LOG_INFO("TableScanExec", "init: table=" + tableId_ +
             " chunks=" + std::to_string(chunkList_.size()) +
             " columns=" + std::to_string(requiredColumns_.size()));
}

std::optional<EncryptedBatch> TableScanExec::next() {
    if (!initialized_) init();
    if (chunkIdx_ >= chunkList_.size()) return std::nullopt;

    auto& meta = chunkList_[chunkIdx_];
    ++chunkIdx_;

    EncryptedBatch batch;
    batch.chunkId        = meta.chunkId;
    batch.logicalRowCount = meta.logicalRowCount;

    for (auto& colName : requiredColumns_) {
        auto chunk = storage_->getChunk(tableId_, colName,
                                         meta.partitionId, meta.chunkId);
        batch.columns[colName] = std::move(chunk);
    }

    LOG_DEBUG("TableScanExec", "next: chunk=" + std::to_string(meta.chunkId) +
              " rows=" + std::to_string(meta.logicalRowCount));
    return batch;
}

void TableScanExec::reset() {
    chunkIdx_    = 0;
    initialized_ = false;
}

// ── FilterExec ────────────────────────────────────────────────────────────

FilterExec::FilterExec(std::shared_ptr<PhysicalOperator> child,
                        std::shared_ptr<Expression>       predicate,
                        std::shared_ptr<FHEKeyContext>    ctx)
    : child_(std::move(child))
    , predicate_(std::move(predicate))
    , executor_(ctx) {}

std::optional<EncryptedBatch> FilterExec::next() {
    auto batch = child_->next();
    if (!batch) return std::nullopt;

    LOG_DEBUG("FilterExec", "evaluating predicate on chunk=" +
              std::to_string(batch->chunkId));

    auto mask = executor_.evaluate(*predicate_, *batch);
    batch->selectionMask = std::move(mask);
    return batch;
}

void FilterExec::reset() { child_->reset(); }

// ── ProjectExec ───────────────────────────────────────────────────────────

ProjectExec::ProjectExec(std::shared_ptr<PhysicalOperator> child,
                          std::vector<std::string>           outputColumns)
    : child_(std::move(child))
    , outputColumns_(std::move(outputColumns)) {}

std::optional<EncryptedBatch> ProjectExec::next() {
    auto batch = child_->next();
    if (!batch) return std::nullopt;

    EncryptedBatch projected;
    projected.chunkId         = batch->chunkId;
    projected.logicalRowCount = batch->logicalRowCount;
    projected.selectionMask   = batch->selectionMask;

    for (auto& col : outputColumns_) {
        auto it = batch->columns.find(col);
        if (it == batch->columns.end())
            throw std::runtime_error("ProjectExec: column not found: " + col);
        projected.columns[col] = std::move(it->second);
    }

    LOG_DEBUG("ProjectExec", "projected " + std::to_string(outputColumns_.size()) +
              " columns from chunk=" + std::to_string(batch->chunkId));
    return projected;
}

void ProjectExec::reset() { child_->reset(); }

} // namespace proto1
