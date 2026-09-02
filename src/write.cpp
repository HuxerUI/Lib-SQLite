#include <huxerui/sqlite.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "sql_builder.h"

namespace huxerui::sqlite::detail {
namespace {

[[nodiscard]] const ColumnSchema& FindColumn(
    const TableSchema& table,
    const std::string& name,
    const char* operation
) {
  const auto column = std::find_if(
      table.columns.begin(), table.columns.end(),
      [&name](const ColumnSchema& candidate) {
        return candidate.name == name;
      }
  );
  if (column == table.columns.end()) {
    throw std::invalid_argument(
        std::string{"HuxerUI SQLite "} + operation + " references an undeclared column"
    );
  }
  return *column;
}

[[nodiscard]] Error AssignmentError(
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

struct ValidatedAssignment final {
  const ColumnSchema* column;
  Value value;
};

[[nodiscard]] Result<std::vector<ValidatedAssignment>> ValidateAssignments(
    const TableSchema& table,
    std::vector<AssignmentData> assignments,
    const char* description,
    std::string operation
) {
  if (assignments.empty()) {
    throw std::invalid_argument(
        std::string{"HuxerUI SQLite "} + description +
        " requires at least one field assignment"
    );
  }
  std::vector<ValidatedAssignment> validated;
  validated.reserve(assignments.size());
  std::unordered_set<std::string> assigned_columns;
  for (AssignmentData& assignment : assignments) {
    if (assignment.table_name != table.name) {
      throw std::invalid_argument(
          std::string{"HuxerUI SQLite "} + description +
          " contains a field from a different table"
      );
    }
    const ColumnSchema& column = FindColumn(table, assignment.column_name, description);
    if (column.primary_key) {
      throw std::invalid_argument(
          std::string{"HuxerUI SQLite "} + description +
          " cannot assign the primary key"
      );
    }
    if (!assigned_columns.insert(column.name).second) {
      throw std::invalid_argument(
          std::string{"HuxerUI SQLite "} + description +
          " contains a duplicate field assignment"
      );
    }
    if (!assignment.value) {
      return AssignmentError(
          assignment.value.Error(), table, column, operation
      );
    }
    if (std::holds_alternative<Null>(*assignment.value) && !column.nullable) {
      return Error{
          ErrorCode::Decode,
          "Column " + table.name + "." + column.name + " encoded NULL for a required field",
          operation,
      };
    }
    if (!ValueMatchesAffinity(*assignment.value, column.affinity)) {
      return Error{
          ErrorCode::Decode,
          "Column " + table.name + "." + column.name + " encoded an incompatible value",
          operation,
      };
    }
    validated.push_back(ValidatedAssignment{
        .column = &column,
        .value = std::move(*assignment.value),
    });
  }
  return validated;
}

void ValidateConflictTarget(
    const TableSchema& table,
    const ConflictTargetData& conflict_target
) {
  if (conflict_target.table_name != table.name) {
    throw std::invalid_argument(
        "HuxerUI SQLite upsert conflict target belongs to a different table"
    );
  }
  if (conflict_target.column_names.empty()) {
    throw std::invalid_argument("HuxerUI SQLite upsert conflict target is empty");
  }

  std::unordered_set<std::string> target_columns;
  std::vector<const ColumnSchema*> columns;
  columns.reserve(conflict_target.column_names.size());
  for (const std::string& name : conflict_target.column_names) {
    if (!target_columns.insert(name).second) {
      throw std::invalid_argument(
          "HuxerUI SQLite upsert conflict target contains a duplicate column"
      );
    }
    const ColumnSchema& column = FindColumn(table, name, "upsert conflict target");
    if (column.auto_increment) {
      throw std::invalid_argument(
          "HuxerUI SQLite upsert cannot target an omitted auto-increment column"
      );
    }
    columns.push_back(&column);
  }

  bool unique = columns.size() == 1 &&
                (columns.front()->primary_key || columns.front()->unique);
  for (const IndexSchema& index : table.indexes) {
    if (index.unique && index.columns == conflict_target.column_names) {
      unique = true;
    }
  }
  if (!unique) {
    throw std::invalid_argument(
        "HuxerUI SQLite upsert conflict target is not a declared unique key"
    );
  }
}

void AppendConflictTarget(std::string& sql, const ConflictTargetData& conflict_target) {
  sql += " ON CONFLICT (";
  for (std::size_t index = 0; index < conflict_target.column_names.size(); ++index) {
    if (index > 0) {
      sql += ", ";
    }
    sql += QuoteIdentifier(conflict_target.column_names[index]);
  }
  sql += ")";
}

} // namespace

Result<CrudStatement> BuildUpdateFieldsStatement(
    const TableSchema& table,
    Result<Value> key,
    std::vector<AssignmentData> assignments
) {
  auto validated_assignments = ValidateAssignments(
      table, std::move(assignments), "partial update", "encode partial update field"
  );
  if (!validated_assignments) {
    return validated_assignments.Error();
  }
  const ColumnSchema& key_column = PrimaryKey(table);
  auto validated_key = ValidatePrimaryKey(table, key_column, std::move(key));
  if (!validated_key) {
    return validated_key.Error();
  }

  CrudStatement statement{
      .sql = "UPDATE " + QuoteIdentifier(table.name) + " SET ",
  };
  statement.parameters.reserve(validated_assignments->size() + 1);
  for (std::size_t index = 0; index < validated_assignments->size(); ++index) {
    ValidatedAssignment& assignment = (*validated_assignments)[index];
    if (index > 0) {
      statement.sql += ", ";
    }
    statement.sql += QuoteIdentifier(assignment.column->name);
    statement.sql += " = ?";
    statement.parameters.push_back(std::move(assignment.value));
  }
  statement.sql += " WHERE ";
  statement.sql += QuoteIdentifier(key_column.name);
  statement.sql += " = ?";
  statement.parameters.push_back(std::move(*validated_key));
  return statement;
}

Result<CrudStatement> BuildUpsertStatement(
    const TableSchema& table,
    const void* record,
    ConflictTargetData conflict_target,
    std::vector<AssignmentData> assignments
) {
  ValidateConflictTarget(table, conflict_target);
  auto validated_assignments = ValidateAssignments(
      table, std::move(assignments), "upsert update", "encode upsert update field"
  );
  if (!validated_assignments) {
    return validated_assignments.Error();
  }
  auto statement = BuildInsertStatement(table, record, ConflictPolicy::Abort);
  if (!statement) {
    return statement.Error();
  }

  AppendConflictTarget(statement->sql, conflict_target);
  statement->sql += " DO UPDATE SET ";
  statement->parameters.reserve(
      statement->parameters.size() + validated_assignments->size()
  );
  for (std::size_t index = 0; index < validated_assignments->size(); ++index) {
    ValidatedAssignment& assignment = (*validated_assignments)[index];
    if (index > 0) {
      statement->sql += ", ";
    }
    statement->sql += QuoteIdentifier(assignment.column->name);
    statement->sql += " = ?";
    statement->parameters.push_back(std::move(assignment.value));
  }
  return statement;
}

Result<CrudStatement> BuildUpsertDoNothingStatement(
    const TableSchema& table,
    const void* record,
    ConflictTargetData conflict_target
) {
  ValidateConflictTarget(table, conflict_target);
  auto statement = BuildInsertStatement(table, record, ConflictPolicy::Abort);
  if (!statement) {
    return statement.Error();
  }
  AppendConflictTarget(statement->sql, conflict_target);
  statement->sql += " DO NOTHING";
  return statement;
}

} // namespace huxerui::sqlite::detail
