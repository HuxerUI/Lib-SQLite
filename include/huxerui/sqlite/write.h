#pragma once

/// @file
/// Typed partial-update and upsert expression API for HuxerUI SQLite.

#include <huxerui/sqlite.h>

namespace huxerui::sqlite {

namespace detail {

struct AssignmentData final {
  std::string table_name;
  std::string column_name;
  Result<Value> value;
};

struct ConflictTargetData final {
  std::string table_name;
  std::vector<std::string> column_names;
};

struct AssignmentAccess;
struct ConflictTargetAccess;

struct ColumnExpressionAccess final {
  template <class Record, auto Member>
  [[nodiscard]] static const std::string&
  TableName(const ColumnExpression<Record, Member>& column) noexcept {
    return column.table_name_;
  }

  template <class Record, auto Member>
  [[nodiscard]] static const std::string&
  ColumnName(const ColumnExpression<Record, Member>& column) noexcept {
    return column.column_name_;
  }
};

[[nodiscard]] Result<CrudStatement> BuildUpdateFieldsStatement(
    const TableSchema& table,
    Result<Value> key,
    std::vector<AssignmentData> assignments
);
[[nodiscard]] Result<CrudStatement> BuildUpsertStatement(
    const TableSchema& table,
    const void* record,
    ConflictTargetData conflict_target,
    std::vector<AssignmentData> assignments
);
[[nodiscard]] Result<CrudStatement> BuildUpsertDoNothingStatement(
    const TableSchema& table,
    const void* record,
    ConflictTargetData conflict_target
);

} // namespace detail

/// Encoded assignment of one declared record member to a new value.
///
/// Create assignments with Set(). They retain table identity and are accepted
/// only by typed partial-update and upsert operations for the same Record.
///
/// @tparam Record Record type owning the assigned member.
template <class Record> class FieldAssignment final {
public:
  FieldAssignment(const FieldAssignment&) = default;
  FieldAssignment(FieldAssignment&&) noexcept = default;
  FieldAssignment& operator=(const FieldAssignment&) = default;
  FieldAssignment& operator=(FieldAssignment&&) noexcept = default;

private:
  explicit FieldAssignment(detail::AssignmentData data) : data_(std::move(data)) {}

  friend struct detail::AssignmentAccess;

  detail::AssignmentData data_;
};

/// Ordered set of declared columns identifying an upsert uniqueness constraint.
///
/// Create conflict targets with OnConflict(). The target must match a primary
/// key, a Unique column, or the ordered columns of a unique Index.
///
/// @tparam Record Record type associated with the target table.
template <class Record> class ConflictTarget final {
public:
  ConflictTarget(const ConflictTarget&) = default;
  ConflictTarget(ConflictTarget&&) noexcept = default;
  ConflictTarget& operator=(const ConflictTarget&) = default;
  ConflictTarget& operator=(ConflictTarget&&) noexcept = default;

private:
  explicit ConflictTarget(detail::ConflictTargetData data) : data_(std::move(data)) {}

  friend struct detail::ConflictTargetAccess;

  detail::ConflictTargetData data_;
};

/// Selects the `DO NOTHING` branch of a typed upsert.
struct DoNothing final {};

namespace detail {

struct AssignmentAccess final {
  template <class Record>
  [[nodiscard]] static FieldAssignment<Record> Create(AssignmentData data) {
    return FieldAssignment<Record>{std::move(data)};
  }

  template <class Record>
  static void Append(
      std::vector<AssignmentData>& output,
      const FieldAssignment<Record>& assignment
  ) {
    output.push_back(assignment.data_);
  }

  template <class Record>
  static void Append(
      std::vector<AssignmentData>& output,
      FieldAssignment<Record>&& assignment
  ) {
    output.push_back(std::move(assignment.data_));
  }
};

struct ConflictTargetAccess final {
  template <class Record>
  [[nodiscard]] static ConflictTarget<Record> Create(ConflictTargetData data) {
    return ConflictTarget<Record>{std::move(data)};
  }

  template <class Record>
  [[nodiscard]] static ConflictTargetData Take(ConflictTarget<Record> target) {
    return std::move(target.data_);
  }
};

template <class Record, class... Assignments>
[[nodiscard]] std::vector<AssignmentData> CollectAssignments(
    Assignments&&... assignments
) {
  std::vector<AssignmentData> assignment_data;
  assignment_data.reserve(sizeof...(Assignments));
  (AssignmentAccess::Append(
       assignment_data, std::forward<Assignments>(assignments)
   ),
   ...);
  return assignment_data;
}

} // namespace detail

/// Creates an explicit upsert conflict target.
///
/// @tparam Record Record type shared by every column.
/// @tparam FirstMember First member in the uniqueness constraint.
/// @tparam Members Remaining members in the uniqueness constraint.
/// @param first First typed column in the target.
/// @param columns Remaining typed columns in declared index order.
/// @return A conflict target consumed by Transaction::Upsert() or
/// Database::UpsertAsync().
/// @throws std::invalid_argument If columns belong to different table objects.
///
/// @code
/// auto target = sqlite::OnConflict(users.Column<&User::organization_id>(), users.Column<&User::name>());
/// @endcode
template <class Record, auto FirstMember, auto... Members>
[[nodiscard]] ConflictTarget<Record> OnConflict(
    const ColumnExpression<Record, FirstMember>& first,
    const ColumnExpression<Record, Members>&... columns
) {
  detail::ConflictTargetData data{
      .table_name = detail::ColumnExpressionAccess::TableName(first),
  };
  data.column_names.reserve(1 + sizeof...(Members));
  data.column_names.push_back(detail::ColumnExpressionAccess::ColumnName(first));
  const auto append_column = [&data](const auto& column) {
    if (detail::ColumnExpressionAccess::TableName(column) != data.table_name) {
      throw std::invalid_argument(
          "HuxerUI SQLite conflict target contains a column from a different table"
      );
    }
    data.column_names.push_back(detail::ColumnExpressionAccess::ColumnName(column));
  };
  (append_column(columns), ...);
  return detail::ConflictTargetAccess::Create<Record>(std::move(data));
}

/// Creates one typed field assignment.
///
/// The value is owned and encoded when Set() is called. SQL receives only the
/// resulting bound parameter; application data is never interpolated.
///
/// @tparam Record Record type owning the column.
/// @tparam Member Pointer to the assigned record member.
/// @tparam T Input type constructible as the member type.
/// @param column Typed destination column.
/// @param value New application value.
/// @return An assignment consumed by a partial update or upsert.
///
/// @code
/// auto result = co_await database.UpdateFieldsAsync(
///     users, user_id, sqlite::Set(users.Column<&User::active>(), true), sqlite::Set(users.Column<&User::name>(), "Ada"));
/// @endcode
template <class Record, auto Member, class T>
  requires std::constructible_from<
      typename ColumnExpression<Record, Member>::MemberType,
      T>
[[nodiscard]] FieldAssignment<Record> Set(
    const ColumnExpression<Record, Member>& column,
    T&& value
) {
  using MemberType = typename ColumnExpression<Record, Member>::MemberType;
  MemberType owned_value(std::forward<T>(value));
  return detail::AssignmentAccess::Create<Record>(detail::AssignmentData{
      .table_name = detail::ColumnExpressionAccess::TableName(column),
      .column_name = detail::ColumnExpressionAccess::ColumnName(column),
      .value = ValueCodec<MemberType>::Encode(owned_value),
  });
}

template <class Record, detail::EncodableValue Key, class... Assignments>
  requires(sizeof...(Assignments) > 0) &&
          (std::same_as<std::remove_cvref_t<Assignments>, FieldAssignment<Record>> && ...)
Result<ExecuteResult> Transaction::UpdateFields(
    const Table<Record>& table,
    const Key& key,
    Assignments&&... assignments
) {
  auto statement = detail::BuildUpdateFieldsStatement(
      table.schema_, ValueCodec<std::remove_cvref_t<Key>>::Encode(key),
      detail::CollectAssignments<Record>(std::forward<Assignments>(assignments)...)
  );
  if (!statement) {
    return statement.Error();
  }
  return Execute(std::move(statement->sql), std::move(statement->parameters));
}

template <class Record, detail::EncodableValue Key, class... Assignments>
  requires(sizeof...(Assignments) > 0) &&
          (std::same_as<std::remove_cvref_t<Assignments>, FieldAssignment<Record>> && ...)
Task<Result<ExecuteResult>> Database::UpdateFieldsAsync(
    const Table<Record>& table,
    const Key& key,
    Assignments&&... assignments
) const {
  return ExecuteCrudAsync(
      state_, detail::BuildUpdateFieldsStatement(
                  table.schema_, ValueCodec<std::remove_cvref_t<Key>>::Encode(key),
                  detail::CollectAssignments<Record>(
                      std::forward<Assignments>(assignments)...
                  )
              )
  );
}

template <class Record, class... Assignments>
  requires(sizeof...(Assignments) > 0) &&
          (std::same_as<std::remove_cvref_t<Assignments>, FieldAssignment<Record>> && ...)
Result<ExecuteResult> Transaction::Upsert(
    const Table<Record>& table,
    const Record& record,
    ConflictTarget<Record> conflict_target,
    Assignments&&... assignments
) {
  auto statement = detail::BuildUpsertStatement(
      table.schema_, &record,
      detail::ConflictTargetAccess::Take(std::move(conflict_target)),
      detail::CollectAssignments<Record>(std::forward<Assignments>(assignments)...)
  );
  if (!statement) {
    return statement.Error();
  }
  return Execute(std::move(statement->sql), std::move(statement->parameters));
}

template <class Record>
Result<ExecuteResult> Transaction::Upsert(
    const Table<Record>& table,
    const Record& record,
    ConflictTarget<Record> conflict_target,
    DoNothing
) {
  auto statement = detail::BuildUpsertDoNothingStatement(
      table.schema_, &record,
      detail::ConflictTargetAccess::Take(std::move(conflict_target))
  );
  if (!statement) {
    return statement.Error();
  }
  return Execute(std::move(statement->sql), std::move(statement->parameters));
}

template <class Record, class... Assignments>
  requires(sizeof...(Assignments) > 0) &&
          (std::same_as<std::remove_cvref_t<Assignments>, FieldAssignment<Record>> && ...)
Task<Result<ExecuteResult>> Database::UpsertAsync(
    const Table<Record>& table,
    const Record& record,
    ConflictTarget<Record> conflict_target,
    Assignments&&... assignments
) const {
  return ExecuteCrudAsync(
      state_, detail::BuildUpsertStatement(
                  table.schema_, &record,
                  detail::ConflictTargetAccess::Take(std::move(conflict_target)),
                  detail::CollectAssignments<Record>(
                      std::forward<Assignments>(assignments)...
                  )
              )
  );
}

template <class Record>
Task<Result<ExecuteResult>> Database::UpsertAsync(
    const Table<Record>& table,
    const Record& record,
    ConflictTarget<Record> conflict_target,
    DoNothing
) const {
  return ExecuteCrudAsync(
      state_, detail::BuildUpsertDoNothingStatement(
                  table.schema_, &record,
                  detail::ConflictTargetAccess::Take(std::move(conflict_target))
              )
  );
}

} // namespace huxerui::sqlite
