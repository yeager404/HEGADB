#pragma once
// Plan validator — checks a physical plan before execution.

#include "proto1/operators.h"
#include "proto1/catalog.h"
#include "proto1/expression.h"
#include <string>
#include <vector>

namespace proto1 {

class PlanValidator {
public:
    explicit PlanValidator(const Catalog& catalog);

    // Validate that all column references in the expression exist in the table.
    void validateExpression(const Expression& expr,
                            const std::string& tableId) const;

    // Validate projection columns exist.
    void validateProjection(const std::vector<std::string>& cols,
                            const std::string& tableId) const;

private:
    const Catalog& catalog_;
};

} // namespace proto1
