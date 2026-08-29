#pragma once
// Catalog: table/column schema registry.

#include "proto1/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace proto1 {

struct TableSchema {
    std::string              tableId;
    std::vector<ColumnSchema> columns;

    const ColumnSchema& column(const std::string& name) const {
        for (auto& c : columns)
            if (c.name == name) return c;
        throw std::runtime_error("Column not found: " + name);
    }

    bool hasColumn(const std::string& name) const {
        for (auto& c : columns)
            if (c.name == name) return true;
        return false;
    }
};

class Catalog {
public:
    void registerTable(TableSchema schema) {
        tables_[schema.tableId] = std::move(schema);
    }

    const TableSchema& getTable(const std::string& tableId) const {
        auto it = tables_.find(tableId);
        if (it == tables_.end())
            throw std::runtime_error("Table not found: " + tableId);
        return it->second;
    }

    bool hasTable(const std::string& tableId) const {
        return tables_.count(tableId) > 0;
    }

private:
    std::unordered_map<std::string, TableSchema> tables_;
};

} // namespace proto1
