#include <huxerui/sqlite.h>

#include <algorithm>
#include <stdexcept>

#include "sql_builder.h"

namespace huxerui::sqlite::detail {
namespace {

[[nodiscard]] const char* ConflictSql(ConflictPolicy policy) noexcept {
  switch (policy) {
  case ConflictPolicy::Abort:
    return "INSERT OR ABORT INTO ";
  case ConflictPolicy::Fail:
    return "INSERT OR FAIL INTO ";
  case ConflictPolicy::Ignore:
    return "INSERT OR IGNORE INTO ";
  case ConflictPolicy::Replace:
    return "INSERT OR REPLACE INTO ";
  case ConflictPolicy::Rollback:
    return "INSERT OR ROLLBACK INTO ";
  }
  return "INSERT OR ABORT INTO ";
}

[[nodiscard]] Error ColumnError(
    const Error& error,
    const TableSchema& table,
    const ColumnSchema& column,
    std::string operation
) {
  return Error{
      error.Code(),
      "Column " + table.name + "." + column.name + ": " + error.Message(),
      std::move(operation),
      error.SqlitePrimaryCode(),
      error.SqliteExtendedCode(),
  };
}

[[nodiscard]] Result<Value> EncodeColumn(
    const TableSchema& table,
    const ColumnSchema& column,
    const void* record,
    std::string operation
) {
  if (!column.encode_member) {
    throw std::logic_error("HuxerUI SQLite column does not have a record encoder");
  }
  auto encoded = column.encode_member(record);
  if (!encoded) {
    return ColumnError(encoded.Error(), table, column, std::move(operation));
  }
  if (std::holds_alternative<Null>(*encoded) && !column.nullable) {
    return Error{
        ErrorCode::Decode,
        "Column " + table.name + "." + column.name + " encoded NULL for a required field",
        std::move(operation),
    };
  }
  if (!ValueMatchesAffinity(*encoded, column.affinity)) {
    return Error{
        ErrorCode::Decode,
        "Column " + table.name + "." + column.name + " encoded an incompatible value",
        std::move(operation),
    };
  }
  return encoded;
}

} // namespace

Result<CrudStatement> BuildInsertStatement(
    const TableSchema& table,
    const void* record,
    ConflictPolicy conflict_policy
) {
  CrudStatement statement;
  statement.sql = ConflictSql(conflict_policy);
  statement.sql += QuoteIdentifier(table.name);
  statement.returns_generated_key = std::any_of(
      table.columns.begin(), table.columns.end(),
      [](const ColumnSchema& column) {
        return column.auto_increment;
      }
  );

  std::vector<const ColumnSchema*> inserted_columns;
  inserted_columns.reserve(table.columns.size());
  for (const ColumnSchema& column : table.columns) {
    if (!column.auto_increment) {
      inserted_columns.push_back(&column);
    }
  }
  if (inserted_columns.empty()) {
    statement.sql += " DEFAULT VALUES";
    return statement;
  }

  statement.sql += " (";
  for (std::size_t index = 0; index < inserted_columns.size(); ++index) {
    if (index > 0) {
      statement.sql += ", ";
    }
    statement.sql += QuoteIdentifier(inserted_columns[index]->name);
  }
  statement.sql += ") VALUES (";
  statement.parameters.reserve(inserted_columns.size());
  for (std::size_t index = 0; index < inserted_columns.size(); ++index) {
    if (index > 0) {
      statement.sql += ", ";
    }
    statement.sql += "?";
    auto value = EncodeColumn(table, *inserted_columns[index], record, "encode insert record");
    if (!value) {
      return value.Error();
    }
    statement.parameters.push_back(std::move(*value));
  }
  statement.sql += ")";
  return statement;
}

Result<CrudStatement> BuildUpdateStatement(const TableSchema& table, const void* record) {
  const ColumnSchema& key_column = PrimaryKey(table);
  CrudStatement statement;
  statement.sql = "UPDATE " + QuoteIdentifier(table.name) + " SET ";

  bool has_update = false;
  for (const ColumnSchema& column : table.columns) {
    if (column.primary_key) {
      continue;
    }
    if (has_update) {
      statement.sql += ", ";
    }
    has_update = true;
    statement.sql += QuoteIdentifier(column.name);
    statement.sql += " = ?";
    auto value = EncodeColumn(table, column, record, "encode update record");
    if (!value) {
      return value.Error();
    }
    statement.parameters.push_back(std::move(*value));
  }
  if (!has_update) {
    throw std::invalid_argument(
        "HuxerUI SQLite update requires at least one non-primary-key column"
    );
  }

  auto key = EncodeColumn(table, key_column, record, "encode update primary key");
  if (!key) {
    return key.Error();
  }
  statement.sql += " WHERE ";
  statement.sql += QuoteIdentifier(key_column.name);
  statement.sql += " = ?";
  statement.parameters.push_back(std::move(*key));
  return statement;
}

Result<CrudStatement> BuildFindStatement(const TableSchema& table, Result<Value> key) {
  const ColumnSchema& key_column = PrimaryKey(table);
  auto validated_key = ValidatePrimaryKey(table, key_column, std::move(key));
  if (!validated_key) {
    return validated_key.Error();
  }
  CrudStatement statement;
  statement.sql = "SELECT " + SelectColumns(table) + " FROM " + QuoteIdentifier(table.name) +
                  " WHERE " + QuoteIdentifier(key_column.name) + " = ? LIMIT 1";
  statement.parameters.push_back(std::move(*validated_key));
  return statement;
}

Result<CrudStatement> BuildDeleteStatement(const TableSchema& table, Result<Value> key) {
  const ColumnSchema& key_column = PrimaryKey(table);
  auto validated_key = ValidatePrimaryKey(table, key_column, std::move(key));
  if (!validated_key) {
    return validated_key.Error();
  }
  CrudStatement statement;
  statement.sql = "DELETE FROM " + QuoteIdentifier(table.name) + " WHERE " +
                  QuoteIdentifier(key_column.name) + " = ?";
  statement.parameters.push_back(std::move(*validated_key));
  return statement;
}

Result<void> DecodeRecord(const TableSchema& table, void* record, const RowView& row) {
  if (row.ColumnCount() != table.columns.size()) {
    return Error{
        ErrorCode::Decode,
        "Typed query returned an unexpected number of columns for table " + table.name,
        "decode typed record",
    };
  }
  for (std::size_t index = 0; index < table.columns.size(); ++index) {
    const ColumnSchema& column = table.columns[index];
    if (!column.decode_member) {
      throw std::logic_error("HuxerUI SQLite column does not have a record decoder");
    }
    const Value& value = row.Value(index);
    if (std::holds_alternative<Null>(value) && !column.nullable) {
      return Error{
          ErrorCode::Decode,
          "Column " + table.name + "." + column.name + " unexpectedly contains NULL",
          "decode typed record",
      };
    }
    auto decoded = column.decode_member(record, value);
    if (!decoded) {
      return ColumnError(decoded.Error(), table, column, "decode typed record");
    }
  }
  return {};
}

InsertResult MakeInsertResult(bool returns_generated_key, const ExecuteResult& result) {
  InsertResult insert_result{
      .rows_affected = result.rows_affected,
  };
  if (returns_generated_key && result.rows_affected > 0) {
    insert_result.generated_primary_key = result.last_insert_row_id;
  }
  return insert_result;
}

} // namespace huxerui::sqlite::detail
