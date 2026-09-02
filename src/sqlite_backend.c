#define SQLITE_API static
#include "../third_party/sqlite/sqlite3.c"

#include "sqlite_backend.h"

int huxer_sqlite_open(const char* path, int mode, HuxerSqliteConnection** connection) {
  int flags = SQLITE_OPEN_FULLMUTEX;
  if (mode == HUXER_SQLITE_OPEN_READ_ONLY) {
    flags |= SQLITE_OPEN_READONLY;
  } else if (mode == HUXER_SQLITE_OPEN_READ_WRITE) {
    flags |= SQLITE_OPEN_READWRITE;
  } else {
    flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  }
  return sqlite3_open_v2(path, (sqlite3**)connection, flags, 0);
}

int huxer_sqlite_close(HuxerSqliteConnection* connection) {
  return sqlite3_close_v2((sqlite3*)connection);
}

void huxer_sqlite_interrupt(HuxerSqliteConnection* connection) {
  sqlite3_interrupt((sqlite3*)connection);
}

int huxer_sqlite_extended_result_codes(HuxerSqliteConnection* connection, int enabled) {
  return sqlite3_extended_result_codes((sqlite3*)connection, enabled);
}

int huxer_sqlite_extended_error_code(HuxerSqliteConnection* connection) {
  return sqlite3_extended_errcode((sqlite3*)connection);
}

const char* huxer_sqlite_error_message(HuxerSqliteConnection* connection) {
  return sqlite3_errmsg((sqlite3*)connection);
}

const char* huxer_sqlite_error_string(int result_code) {
  return sqlite3_errstr(result_code);
}

int huxer_sqlite_busy_timeout(HuxerSqliteConnection* connection, int milliseconds) {
  return sqlite3_busy_timeout((sqlite3*)connection, milliseconds);
}

int huxer_sqlite_execute(HuxerSqliteConnection* connection, const char* sql) {
  return sqlite3_exec((sqlite3*)connection, sql, 0, 0, 0);
}

static int huxer_sqlite_transaction_authorizer(
    void* context,
    int action,
    const char* first,
    const char* second,
    const char* database,
    const char* trigger
) {
  (void)context;
  (void)first;
  (void)second;
  (void)database;
  (void)trigger;
  if (action == SQLITE_TRANSACTION || action == SQLITE_SAVEPOINT) {
    return SQLITE_DENY;
  }
  return SQLITE_OK;
}

int huxer_sqlite_restrict_transaction_control(HuxerSqliteConnection* connection, int restricted) {
  return sqlite3_set_authorizer(
      (sqlite3*)connection, restricted ? huxer_sqlite_transaction_authorizer : 0, 0
  );
}

int huxer_sqlite_is_autocommit(HuxerSqliteConnection* connection) {
  return sqlite3_get_autocommit((sqlite3*)connection);
}

int huxer_sqlite_prepare(
    HuxerSqliteConnection* connection,
    const char* sql,
    int size,
    HuxerSqliteStatement** statement,
    const char** tail
) {
  return sqlite3_prepare_v3(
      (sqlite3*)connection, sql, size, SQLITE_PREPARE_PERSISTENT, (sqlite3_stmt**)statement, tail
  );
}

int huxer_sqlite_finalize(HuxerSqliteStatement* statement) {
  return sqlite3_finalize((sqlite3_stmt*)statement);
}

int huxer_sqlite_step(HuxerSqliteStatement* statement) {
  return sqlite3_step((sqlite3_stmt*)statement);
}

int huxer_sqlite_reset(HuxerSqliteStatement* statement) {
  return sqlite3_reset((sqlite3_stmt*)statement);
}

int huxer_sqlite_clear_bindings(HuxerSqliteStatement* statement) {
  return sqlite3_clear_bindings((sqlite3_stmt*)statement);
}

int huxer_sqlite_column_count(HuxerSqliteStatement* statement) {
  return sqlite3_column_count((sqlite3_stmt*)statement);
}

const char* huxer_sqlite_column_name(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_name((sqlite3_stmt*)statement, column);
}

int huxer_sqlite_column_type(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_type((sqlite3_stmt*)statement, column);
}

int64_t huxer_sqlite_column_int64(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_int64((sqlite3_stmt*)statement, column);
}

double huxer_sqlite_column_double(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_double((sqlite3_stmt*)statement, column);
}

const unsigned char* huxer_sqlite_column_text(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_text((sqlite3_stmt*)statement, column);
}

const void* huxer_sqlite_column_blob(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_blob((sqlite3_stmt*)statement, column);
}

int huxer_sqlite_column_bytes(HuxerSqliteStatement* statement, int column) {
  return sqlite3_column_bytes((sqlite3_stmt*)statement, column);
}

int huxer_sqlite_parameter_count(HuxerSqliteStatement* statement) {
  return sqlite3_bind_parameter_count((sqlite3_stmt*)statement);
}

int huxer_sqlite_bind_null(HuxerSqliteStatement* statement, int index) {
  return sqlite3_bind_null((sqlite3_stmt*)statement, index);
}

int huxer_sqlite_bind_int64(HuxerSqliteStatement* statement, int index, int64_t value) {
  return sqlite3_bind_int64((sqlite3_stmt*)statement, index, value);
}

int huxer_sqlite_bind_double(HuxerSqliteStatement* statement, int index, double value) {
  return sqlite3_bind_double((sqlite3_stmt*)statement, index, value);
}

int huxer_sqlite_bind_int(HuxerSqliteStatement* statement, int index, int value) {
  return sqlite3_bind_int((sqlite3_stmt*)statement, index, value);
}

int huxer_sqlite_bind_text(
    HuxerSqliteStatement* statement,
    int index,
    const char* value,
    uint64_t size
) {
  return sqlite3_bind_text64((sqlite3_stmt*)statement, index, value, size, SQLITE_TRANSIENT, SQLITE_UTF8);
}

int huxer_sqlite_bind_blob(
    HuxerSqliteStatement* statement,
    int index,
    const void* value,
    uint64_t size
) {
  static const unsigned char empty_blob = 0;
  const void* bytes = size == 0 ? &empty_blob : value;
  return sqlite3_bind_blob64((sqlite3_stmt*)statement, index, bytes, size, SQLITE_TRANSIENT);
}

int64_t huxer_sqlite_changes(HuxerSqliteConnection* connection) {
  return sqlite3_changes64((sqlite3*)connection);
}

int64_t huxer_sqlite_last_insert_row_id(HuxerSqliteConnection* connection) {
  return sqlite3_last_insert_rowid((sqlite3*)connection);
}

const char* huxer_sqlite_version(void) {
  return sqlite3_libversion();
}

const char* huxer_sqlite_source_id(void) {
  return sqlite3_sourceid();
}

const char* huxer_sqlite_compile_option(int index) {
  return sqlite3_compileoption_get(index);
}
