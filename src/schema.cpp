#include <huxerui/sqlite.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace huxerui::sqlite {
namespace {

[[nodiscard]] bool IsValidUtf8(const std::string& value) noexcept {
  const auto* text = reinterpret_cast<const unsigned char*>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = text[index++];
    if (first <= 0x7F) {
      continue;
    }
    if (first >= 0xC2 && first <= 0xDF) {
      if (index >= value.size() || text[index] < 0x80 || text[index] > 0xBF) {
        return false;
      }
      ++index;
      continue;
    }
    if (first >= 0xE0 && first <= 0xEF) {
      if (index + 1 >= value.size()) {
        return false;
      }
      const unsigned char second = text[index];
      const unsigned char third = text[index + 1];
      const unsigned char second_minimum = first == 0xE0 ? 0xA0 : 0x80;
      const unsigned char second_maximum = first == 0xED ? 0x9F : 0xBF;
      if (second < second_minimum || second > second_maximum || third < 0x80 || third > 0xBF) {
        return false;
      }
      index += 2;
      continue;
    }
    if (first >= 0xF0 && first <= 0xF4) {
      if (index + 2 >= value.size()) {
        return false;
      }
      const unsigned char second = text[index];
      const unsigned char third = text[index + 1];
      const unsigned char fourth = text[index + 2];
      const unsigned char second_minimum = first == 0xF0 ? 0x90 : 0x80;
      const unsigned char second_maximum = first == 0xF4 ? 0x8F : 0xBF;
      if (second < second_minimum || second > second_maximum || third < 0x80 || third > 0xBF ||
          fourth < 0x80 || fourth > 0xBF) {
        return false;
      }
      index += 3;
      continue;
    }
    return false;
  }
  return true;
}

void ValidateName(const std::string& value, const char* description) {
  if (value.empty() || value.find('\0') != std::string::npos || !IsValidUtf8(value)) {
    throw std::invalid_argument(std::string{"HuxerUI SQLite "} + description + " is invalid");
  }
}

[[nodiscard]] std::string CanonicalName(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

} // namespace

namespace detail {

bool ValueMatchesAffinity(const Value& value, StorageAffinity affinity) noexcept {
  if (std::holds_alternative<Null>(value)) {
    return true;
  }
  switch (affinity) {
  case StorageAffinity::Integer:
    return std::holds_alternative<std::int64_t>(value) || std::holds_alternative<bool>(value);
  case StorageAffinity::Real:
    return std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value);
  case StorageAffinity::Text:
    return std::holds_alternative<std::string>(value);
  case StorageAffinity::Blob:
    return std::holds_alternative<Bytes>(value);
  }
  return false;
}

void ValidateTableSchema(TableSchema& table) {
  ValidateName(table.name, "table name");
  if (CanonicalName(table.name).starts_with("sqlite_")) {
    throw std::invalid_argument("HuxerUI SQLite table name uses SQLite's reserved prefix");
  }
  if (table.columns.empty()) {
    throw std::invalid_argument("HuxerUI SQLite table must contain at least one column");
  }

  std::unordered_set<std::string> column_names;
  std::size_t primary_key_count = 0;
  for (const ColumnSchema& column : table.columns) {
    ValidateName(column.name, "column name");
    if (!column_names.insert(CanonicalName(column.name)).second) {
      throw std::invalid_argument("HuxerUI SQLite table contains duplicate column names");
    }
    if (column.primary_key) {
      ++primary_key_count;
      if (column.nullable) {
        throw std::invalid_argument("HuxerUI SQLite primary key column cannot be optional");
      }
    }
    if (column.auto_increment && (!column.primary_key || !column.integer_key_compatible)) {
      throw std::invalid_argument(
          "HuxerUI SQLite auto-increment requires a signed integer primary key"
      );
    }
    if (column.default_value && std::holds_alternative<Null>(*column.default_value) && !column.nullable) {
      throw std::invalid_argument("HuxerUI SQLite non-optional column cannot default to NULL");
    }
    if (column.default_value) {
      const double* number = std::get_if<double>(&*column.default_value);
      if (number && !std::isfinite(*number)) {
        throw std::invalid_argument("HuxerUI SQLite floating-point default must be finite");
      }
      const std::string* text = std::get_if<std::string>(&*column.default_value);
      if (text && (text->find('\0') != std::string::npos || !IsValidUtf8(*text))) {
        throw std::invalid_argument("HuxerUI SQLite text default is not valid UTF-8 text");
      }
    }
    if (column.foreign_key) {
      ValidateName(column.foreign_key->table, "referenced table name");
      ValidateName(column.foreign_key->column, "referenced column name");
    }
  }
  if (primary_key_count != 1) {
    throw std::invalid_argument("HuxerUI SQLite table requires exactly one declared primary key");
  }

  std::unordered_set<std::string> index_names;
  for (IndexSchema& index : table.indexes) {
    ValidateName(index.name, "index name");
    if (CanonicalName(index.name).starts_with("sqlite_")) {
      throw std::invalid_argument("HuxerUI SQLite index name uses SQLite's reserved prefix");
    }
    if (!index_names.insert(CanonicalName(index.name)).second) {
      throw std::invalid_argument("HuxerUI SQLite table contains duplicate index names");
    }
    std::unordered_set<const void*> members;
    for (const void* member_key : index.member_keys) {
      if (!members.insert(member_key).second) {
        throw std::invalid_argument("HuxerUI SQLite index contains a duplicate column");
      }
      const auto column = std::find_if(
          table.columns.begin(), table.columns.end(),
          [member_key](const ColumnSchema& candidate) {
            return candidate.member_key == member_key;
          }
      );
      if (column == table.columns.end()) {
        throw std::invalid_argument("HuxerUI SQLite index references an undeclared column");
      }
      index.columns.push_back(column->name);
    }
  }
}

void ValidateSchemaTables(int version, const std::vector<TableSchema>& tables) {
  if (version < 1) {
    throw std::invalid_argument("HuxerUI SQLite schema version must be positive");
  }

  std::unordered_set<std::string> table_names;
  std::unordered_set<std::string> index_names;
  for (const TableSchema& table : tables) {
    if (!table_names.insert(CanonicalName(table.name)).second) {
      throw std::invalid_argument("HuxerUI SQLite schema contains duplicate table names");
    }
    for (const IndexSchema& index : table.indexes) {
      if (!index_names.insert(CanonicalName(index.name)).second) {
        throw std::invalid_argument("HuxerUI SQLite schema contains duplicate index names");
      }
    }
  }

  for (const TableSchema& table : tables) {
    for (const ColumnSchema& column : table.columns) {
      if (!column.foreign_key) {
        continue;
      }
      const auto referenced_table = std::find_if(
          tables.begin(), tables.end(),
          [&column](const TableSchema& candidate) {
            return candidate.name == column.foreign_key->table;
          }
      );
      if (referenced_table == tables.end()) {
        throw std::invalid_argument("HuxerUI SQLite foreign key references an undeclared table");
      }
      const auto referenced_column = std::find_if(
          referenced_table->columns.begin(), referenced_table->columns.end(),
          [&column](const ColumnSchema& candidate) {
            return candidate.name == column.foreign_key->column;
          }
      );
      if (referenced_column == referenced_table->columns.end()) {
        throw std::invalid_argument("HuxerUI SQLite foreign key references an undeclared column");
      }
      if (referenced_column->affinity != column.affinity) {
        throw std::invalid_argument("HuxerUI SQLite foreign key columns have different affinities");
      }
      bool unique_target = referenced_column->primary_key || referenced_column->unique;
      for (const IndexSchema& index : referenced_table->indexes) {
        if (index.unique && index.columns.size() == 1 &&
            index.columns.front() == referenced_column->name) {
          unique_target = true;
        }
      }
      if (!unique_target) {
        throw std::invalid_argument("HuxerUI SQLite foreign key target is not unique");
      }
    }
  }
}

const std::vector<TableSchema>& SchemaAccess::Tables(const Schema& schema) noexcept {
  return schema.tables_;
}

const std::vector<Migration>& MigrationAccess::Values(const Migrations& migrations) noexcept {
  return migrations.migrations_;
}

Result<void> MigrationAccess::Run(const Migration& migration, Transaction& transaction) {
  MigrationContext context{transaction};
  return migration.callback_(context);
}

} // namespace detail

void Migration::Validate() const {
  if (from_version_ < 0 || to_version_ != from_version_ + 1) {
    throw std::invalid_argument(
        "HuxerUI SQLite migration must advance exactly one non-negative version"
    );
  }
  if (!callback_) {
    throw std::invalid_argument("HuxerUI SQLite migration callback is empty");
  }
}

Migrations::Migrations(std::initializer_list<Migration> migrations) : migrations_(migrations) {
  std::sort(migrations_.begin(), migrations_.end(), [](const Migration& left, const Migration& right) {
    return left.FromVersion() < right.FromVersion();
  });
  for (std::size_t index = 1; index < migrations_.size(); ++index) {
    if (migrations_[index - 1].ToVersion() != migrations_[index].FromVersion()) {
      throw std::invalid_argument(
          "HuxerUI SQLite migrations contain a duplicate or missing transition"
      );
    }
  }
}

} // namespace huxerui::sqlite
