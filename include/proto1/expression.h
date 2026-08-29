#pragma once
// Expression tree — independent of SQL parsing.
// Nodes are evaluated by ExpressionExecutor.

#include "proto1/fhe_runtime.h"
#include <memory>
#include <string>
#include <vector>

namespace proto1 {

enum class ExprKind {
    ColumnRef,
    EncryptedLiteral,
    Equal,
    GreaterThan,
    LessThan,
    And,
    Or,
    Not,
};

struct Expression {
    ExprKind kind;

    // ColumnRef
    std::string columnName;

    // EncryptedLiteral — pre-encrypted by the client
    std::shared_ptr<EncryptedVector> literal;

    // Binary / unary children
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right; // null for Not

    static std::shared_ptr<Expression> columnRef(const std::string& name) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::ColumnRef;
        e->columnName = name;
        return e;
    }

    static std::shared_ptr<Expression>
    encLiteral(std::shared_ptr<EncryptedVector> v) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::EncryptedLiteral;
        e->literal = std::move(v);
        return e;
    }

    static std::shared_ptr<Expression>
    equal(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::Equal;
        e->left = std::move(l); e->right = std::move(r);
        return e;
    }

    static std::shared_ptr<Expression>
    gt(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::GreaterThan;
        e->left = std::move(l); e->right = std::move(r);
        return e;
    }

    static std::shared_ptr<Expression>
    lt(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::LessThan;
        e->left = std::move(l); e->right = std::move(r);
        return e;
    }

    static std::shared_ptr<Expression>
    andExpr(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::And;
        e->left = std::move(l); e->right = std::move(r);
        return e;
    }

    static std::shared_ptr<Expression>
    orExpr(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::Or;
        e->left = std::move(l); e->right = std::move(r);
        return e;
    }

    static std::shared_ptr<Expression>
    notExpr(std::shared_ptr<Expression> child) {
        auto e = std::make_shared<Expression>();
        e->kind = ExprKind::Not;
        e->left = std::move(child);
        return e;
    }
};

} // namespace proto1
