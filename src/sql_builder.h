#pragma once

#include <huxerui/sqlite.h>

namespace huxerui::sqlite::detail {

[[nodiscard]] std::string QuoteIdentifier(std::string_view value);
[[nodiscard]] std::string SelectColumns(const TableSchema& table);
[[nodiscard]] const ColumnSchema& PrimaryKey(const TableSchema& table);
[[nodiscard]] Result<Value> ValidatePrimaryKey(
    const TableSchema& table,
    const ColumnSchema& column,
    Result<Value> key
);

} // namespace huxerui::sqlite::detail
