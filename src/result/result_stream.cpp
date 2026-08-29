// EncryptedResultStream implementation.

#include "proto1/result_stream.h"

namespace proto1 {

EncryptedResultStream::EncryptedResultStream(std::shared_ptr<PhysicalOperator> root)
    : root_(std::move(root)) {}

std::optional<EncryptedBatch> EncryptedResultStream::next() {
    return root_->next();
}

std::vector<EncryptedBatch> EncryptedResultStream::collectAll() {
    std::vector<EncryptedBatch> result;
    while (auto batch = next())
        result.push_back(std::move(*batch));
    return result;
}

} // namespace proto1
