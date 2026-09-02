#include <huxerui/sqlite.h>

#include <algorithm>
#include <stdexcept>

#include "sql_builder.h"

namespace huxerui::sqlite::detail {

std::string QuoteIdentifier(std::string_view value) {
  std::string quoted{"\""};
  for (const char character : value) {
    quoted.push_back(character);
    if (character == '"') {
      quoted.push_back('"');
    }
  }
  quoted.push_back('"');
  return quoted;
}

std::string SelectColumns(const TableSchema& table) {
  std::string columns;
  for (std::size_t index = 0; index < table.columns.size(); ++index) {
    if (index > 0) {
      columns += ", ";
    }
    columns += QuoteIdentifier(table.columns[index].name);
  }
  return columns;
}

const ColumnSchema& PrimaryKey(const TableSchema& table) {
  const auto key = std::find_if(
      table.columns.begin(), table.columns.end(),
      [](const ColumnSchema& column) {
        return column.primary_key;
      }
  );
  if (key == table.columns.end()) {
    throw std::invalid_argument(
        "HuxerUI SQLite primary-key operation requires a table with a declared primary key"
    );
  }
  return *key;
}

Result<Value> ValidatePrimaryKey(
    const TableSchema& table,
    const ColumnSchema& column,
    Result<Value> key
) {
  if (!key) {
    const Error& error = key.Error();
    return Error{
        error.Code(),
        "Column " + table.name + "." + column.name + ": " + error.Message(),
        "encode primary key",
        error.SqlitePrimaryCode(),
        error.SqliteExtendedCode(),
    };
  }
  if (std::holds_alternative<Null>(*key)) {
    throw std::invalid_argument("HuxerUI SQLite primary-key value cannot be NULL");
  }
  if (!ValueMatchesAffinity(*key, column.affinity)) {
    throw std::invalid_argument(
        "HuxerUI SQLite primary-key value has an incompatible storage affinity"
    );
  }
  return key;
}

} // namespace huxerui::sqlite::detail
