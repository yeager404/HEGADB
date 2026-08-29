#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>

namespace proto1 {

enum class DataType { INT32, INT64, BOOL };

inline std::string dataTypeName(DataType t) {
    switch (t) {
        case DataType::INT32: return "INT32";
        case DataType::INT64: return "INT64";
        case DataType::BOOL:  return "BOOL";
    }
    throw std::runtime_error("Unknown DataType");
}

struct ColumnSchema {
    std::string name;
    DataType    type;
};

} // namespace proto1
