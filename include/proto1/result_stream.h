#pragma once
// EncryptedResultStream — streaming result from query execution.

#include "proto1/encrypted_batch.h"
#include "proto1/operators.h"
#include <memory>
#include <optional>

namespace proto1 {

class EncryptedResultStream {
public:
    explicit EncryptedResultStream(std::shared_ptr<PhysicalOperator> root);

    // Pull the next result batch. Returns nullopt when done.
    std::optional<EncryptedBatch> next();

    // Collect all batches (convenience, may use significant memory).
    std::vector<EncryptedBatch> collectAll();

private:
    std::shared_ptr<PhysicalOperator> root_;
};

} // namespace proto1
