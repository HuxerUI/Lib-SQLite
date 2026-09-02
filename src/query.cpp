#include <huxerui/sqlite.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>

#include "sql_builder.h"

namespace huxerui::sqlite::detail {
namespace {

[[nodiscard]] Result<CrudStatement> BuildProjectionStatement(
    const TableSchema& table,
    const SelectionSpec& spec,
    std::string projection,
    std::optional<std::int64_t> maximum_rows
) {
  if (spec.predicate && spec.predicate->error) {
    return *spec.predicate->error;
  }

  CrudStatement statement;
  statement.sql = "SELECT " + std::move(projection) + " FROM " + QuoteIdentifier(table.name);
  if (spec.predicate) {
    statement.sql += " WHERE ";
    statement.sql += spec.predicate->sql;
    statement.parameters = spec.predicate->parameters;
  }
  if (!spec.order.empty()) {
    statement.sql += " ORDER BY ";
    for (std::size_t index = 0; index < spec.order.size(); ++index) {
      if (index > 0) {
        statement.sql += ", ";
      }
      statement.sql += QuoteIdentifier(spec.order[index].column_name);
      statement.sql += spec.order[index].direction == SortDirection::Ascending ? " ASC" : " DESC";
    }
  }

  std::optional<std::int64_t> limit = spec.limit;
  if (maximum_rows && (!limit || *limit > *maximum_rows)) {
    limit = maximum_rows;
  }
  if (limit) {
    statement.sql += " LIMIT ?";
    statement.parameters.emplace_back(*limit);
  } else if (spec.offset) {
    statement.sql += " LIMIT -1";
  }
  if (spec.offset) {
    statement.sql += " OFFSET ?";
    statement.parameters.emplace_back(*spec.offset);
  }
  return statement;
}

} // namespace

PredicateData BuildComparisonPredicate(
    std::string table_name,
    std::string column_name,
    std::string operation,
    Result<Value> value
) {
  PredicateData predicate{
      .table_name = std::move(table_name),
  };
  if (!value) {
    predicate.error = value.Error();
    return predicate;
  }
  if (std::holds_alternative<Null>(*value)) {
    if (operation == "=") {
      predicate.sql = QuoteIdentifier(column_name) + " IS NULL";
      return predicate;
    }
    if (operation == "<>") {
      predicate.sql = QuoteIdentifier(column_name) + " IS NOT NULL";
      return predicate;
    }
    throw std::invalid_argument(
        "HuxerUI SQLite NULL only supports equality predicates"
    );
  }
  predicate.sql = QuoteIdentifier(column_name) + " " + std::move(operation) + " ?";
  predicate.parameters.push_back(std::move(*value));
  return predicate;
}

PredicateData BuildInPredicate(
    std::string table_name,
    std::string column_name,
    Result<std::vector<Value>> values
) {
  PredicateData predicate{
      .table_name = std::move(table_name),
  };
  if (!values) {
    predicate.error = values.Error();
    return predicate;
  }
  if (values->empty()) {
    predicate.sql = "0";
    return predicate;
  }
  predicate.sql = QuoteIdentifier(column_name) + " IN (";
  for (std::size_t index = 0; index < values->size(); ++index) {
    if (index > 0) {
      predicate.sql += ", ";
    }
    predicate.sql += "?";
  }
  predicate.sql += ")";
  predicate.parameters = std::move(*values);
  return predicate;
}

PredicateData BuildNullPredicate(
    std::string table_name,
    std::string column_name,
    bool is_null
) {
  return PredicateData{
      .table_name = std::move(table_name),
      .sql = QuoteIdentifier(column_name) + (is_null ? " IS NULL" : " IS NOT NULL"),
  };
}

PredicateData CombinePredicates(
    PredicateData left,
    PredicateData right,
    std::string operation
) {
  if (left.table_name != right.table_name) {
    throw std::invalid_argument(
        "HuxerUI SQLite cannot combine predicates from different tables"
    );
  }
  PredicateData predicate{
      .table_name = std::move(left.table_name),
  };
  if (left.error) {
    predicate.error = std::move(left.error);
    return predicate;
  }
  if (right.error) {
    predicate.error = std::move(right.error);
    return predicate;
  }
  predicate.sql = "(" + std::move(left.sql) + ") " + std::move(operation) + " (" +
                  std::move(right.sql) + ")";
  predicate.parameters = std::move(left.parameters);
  predicate.parameters.insert(
      predicate.parameters.end(),
      std::make_move_iterator(right.parameters.begin()),
      std::make_move_iterator(right.parameters.end())
  );
  return predicate;
}

PredicateData NegatePredicate(PredicateData predicate) {
  if (!predicate.error) {
    predicate.sql = "NOT (" + std::move(predicate.sql) + ")";
  }
  return predicate;
}

void AddWhere(SelectionSpec& spec, const std::string& table_name, PredicateData predicate) {
  if (predicate.table_name != table_name) {
    throw std::invalid_argument(
        "HuxerUI SQLite selection predicate belongs to a different table"
    );
  }
  if (spec.predicate) {
    spec.predicate = CombinePredicates(
        std::move(*spec.predicate), std::move(predicate), "AND"
    );
  } else {
    spec.predicate = std::move(predicate);
  }
}

void AddOrder(
    SelectionSpec& spec,
    const std::string& table_name,
    OrderData order
) {
  if (order.table_name != table_name) {
    throw std::invalid_argument(
        "HuxerUI SQLite selection order belongs to a different table"
    );
  }
  spec.order.push_back(std::move(order));
}

Result<CrudStatement> BuildSelectStatement(
    const TableSchema& table,
    const SelectionSpec& spec,
    std::optional<std::int64_t> maximum_rows
) {
  return BuildProjectionStatement(table, spec, SelectColumns(table), maximum_rows);
}

Result<CrudStatement> BuildCountStatement(
    const TableSchema& table,
    const SelectionSpec& spec
) {
  auto selection = BuildProjectionStatement(table, spec, "1", std::nullopt);
  if (!selection) {
    return selection.Error();
  }
  selection->sql = "SELECT COUNT(*) FROM (" + std::move(selection->sql) + ")";
  return selection;
}

Result<CrudStatement> BuildExistsStatement(
    const TableSchema& table,
    const SelectionSpec& spec
) {
  auto selection = BuildProjectionStatement(table, spec, "1", 1);
  if (!selection) {
    return selection.Error();
  }
  selection->sql = "SELECT EXISTS(" + std::move(selection->sql) + ")";
  return selection;
}

} // namespace huxerui::sqlite::detail
