// PlanValidator implementation.

#include "proto1/plan_validator.h"
#include <stdexcept>

namespace proto1 {

PlanValidator::PlanValidator(const Catalog& catalog) : catalog_(catalog) {}

static void validateExprImpl(const Expression& expr,
                              const TableSchema& schema) {
    switch (expr.kind) {
        case ExprKind::ColumnRef:
            if (!schema.hasColumn(expr.columnName))
                throw std::runtime_error("PlanValidator: unknown column '" +
                                         expr.columnName + "' in table '" +
                                         schema.tableId + "'");
            return;
        case ExprKind::EncryptedLiteral:
            if (!expr.literal)
                throw std::runtime_error("PlanValidator: null encrypted literal");
            return;
        case ExprKind::Not:
            validateExprImpl(*expr.left, schema);
            return;
        default:
            if (!expr.left || !expr.right)
                throw std::runtime_error("PlanValidator: binary expression missing child");
            validateExprImpl(*expr.left, schema);
            validateExprImpl(*expr.right, schema);
    }
}

void PlanValidator::validateExpression(const Expression& expr,
                                        const std::string& tableId) const {
    if (!catalog_.hasTable(tableId))
        throw std::runtime_error("PlanValidator: table not found: " + tableId);
    validateExprImpl(expr, catalog_.getTable(tableId));
}

void PlanValidator::validateProjection(const std::vector<std::string>& cols,
                                        const std::string& tableId) const {
    if (!catalog_.hasTable(tableId))
        throw std::runtime_error("PlanValidator: table not found: " + tableId);
    auto& schema = catalog_.getTable(tableId);
    for (auto& col : cols) {
        if (!schema.hasColumn(col))
            throw std::runtime_error("PlanValidator: projection column '" +
                                     col + "' not in table '" + tableId + "'");
    }
}

} // namespace proto1
