#include "sqlite_backend.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
  HuxerSqliteConnection* connection = nullptr;
  assert(huxer_sqlite_open(":memory:", HUXER_SQLITE_OPEN_READ_WRITE_CREATE, &connection) == HUXER_SQLITE_OK);
  assert(connection != nullptr);
  assert(huxer_sqlite_extended_result_codes(connection, 1) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_execute(
             connection,
             "CREATE TABLE records (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, payload BLOB NOT NULL)"
         ) == HUXER_SQLITE_OK);

  HuxerSqliteStatement* statement = nullptr;
  const char* tail = nullptr;
  assert(huxer_sqlite_prepare(
             connection,
             "INSERT INTO records(name, payload) VALUES (?, ?)",
             -1,
             &statement,
             &tail
         ) == HUXER_SQLITE_OK);
  assert(statement != nullptr);
  assert(huxer_sqlite_parameter_count(statement) == 2);
  assert(huxer_sqlite_bind_text(statement, 1, "first", 5) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_bind_blob(statement, 2, nullptr, 0) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_step(statement) == HUXER_SQLITE_DONE);
  assert(huxer_sqlite_finalize(statement) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_changes(connection) == 1);
  assert(huxer_sqlite_last_insert_row_id(connection) == 1);

  assert(huxer_sqlite_prepare(
             connection,
             "SELECT id, name, payload FROM records",
             -1,
             &statement,
             &tail
         ) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_step(statement) == HUXER_SQLITE_ROW);
  assert(huxer_sqlite_column_count(statement) == 3);
  assert(std::strcmp(huxer_sqlite_column_name(statement, 0), "id") == 0);
  assert(huxer_sqlite_column_type(statement, 0) == HUXER_SQLITE_INTEGER);
  assert(huxer_sqlite_column_int64(statement, 0) == 1);
  assert(huxer_sqlite_column_type(statement, 1) == HUXER_SQLITE_TEXT);
  assert(huxer_sqlite_column_bytes(statement, 1) == 5);
  assert(std::memcmp(huxer_sqlite_column_text(statement, 1), "first", 5) == 0);
  assert(huxer_sqlite_column_type(statement, 2) == HUXER_SQLITE_BLOB);
  assert(huxer_sqlite_column_bytes(statement, 2) == 0);
  assert(huxer_sqlite_step(statement) == HUXER_SQLITE_DONE);
  assert(huxer_sqlite_finalize(statement) == HUXER_SQLITE_OK);

  assert(huxer_sqlite_is_autocommit(connection));
  assert(huxer_sqlite_execute(connection, "BEGIN IMMEDIATE") == HUXER_SQLITE_OK);
  assert(!huxer_sqlite_is_autocommit(connection));
  assert(huxer_sqlite_restrict_transaction_control(connection, 1) == HUXER_SQLITE_OK);
  statement = nullptr;
  assert(huxer_sqlite_prepare(connection, "COMMIT", -1, &statement, &tail) == HUXER_SQLITE_AUTH);
  assert(statement == nullptr);
  assert(huxer_sqlite_restrict_transaction_control(connection, 0) == HUXER_SQLITE_OK);
  assert(huxer_sqlite_execute(connection, "ROLLBACK") == HUXER_SQLITE_OK);
  assert(huxer_sqlite_is_autocommit(connection));

  assert(huxer_sqlite_close(connection) == HUXER_SQLITE_OK);
}
