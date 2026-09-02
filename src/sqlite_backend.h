#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HuxerSqliteConnection HuxerSqliteConnection;
typedef struct HuxerSqliteStatement HuxerSqliteStatement;

enum {
  HUXER_SQLITE_OK = 0,
  HUXER_SQLITE_ERROR = 1,
  HUXER_SQLITE_PERM = 3,
  HUXER_SQLITE_BUSY = 5,
  HUXER_SQLITE_LOCKED = 6,
  HUXER_SQLITE_NOMEM = 7,
  HUXER_SQLITE_READONLY = 8,
  HUXER_SQLITE_INTERRUPT = 9,
  HUXER_SQLITE_IOERR = 10,
  HUXER_SQLITE_CORRUPT = 11,
  HUXER_SQLITE_FULL = 13,
  HUXER_SQLITE_CANTOPEN = 14,
  HUXER_SQLITE_CONSTRAINT = 19,
  HUXER_SQLITE_MISMATCH = 20,
  HUXER_SQLITE_MISUSE = 21,
  HUXER_SQLITE_AUTH = 23,
  HUXER_SQLITE_RANGE = 25,
  HUXER_SQLITE_NOTADB = 26,
  HUXER_SQLITE_ROW = 100,
  HUXER_SQLITE_DONE = 101,
};

enum {
  HUXER_SQLITE_INTEGER = 1,
  HUXER_SQLITE_FLOAT = 2,
  HUXER_SQLITE_TEXT = 3,
  HUXER_SQLITE_BLOB = 4,
  HUXER_SQLITE_NULL = 5,
};

enum {
  HUXER_SQLITE_OPEN_READ_ONLY,
  HUXER_SQLITE_OPEN_READ_WRITE,
  HUXER_SQLITE_OPEN_READ_WRITE_CREATE,
};

int huxer_sqlite_open(const char* path, int mode, HuxerSqliteConnection** connection);
int huxer_sqlite_close(HuxerSqliteConnection* connection);
void huxer_sqlite_interrupt(HuxerSqliteConnection* connection);
int huxer_sqlite_extended_result_codes(HuxerSqliteConnection* connection, int enabled);
int huxer_sqlite_extended_error_code(HuxerSqliteConnection* connection);
const char* huxer_sqlite_error_message(HuxerSqliteConnection* connection);
const char* huxer_sqlite_error_string(int result_code);
int huxer_sqlite_busy_timeout(HuxerSqliteConnection* connection, int milliseconds);
int huxer_sqlite_execute(HuxerSqliteConnection* connection, const char* sql);
int huxer_sqlite_restrict_transaction_control(HuxerSqliteConnection* connection, int restricted);
int huxer_sqlite_is_autocommit(HuxerSqliteConnection* connection);

int huxer_sqlite_prepare(
    HuxerSqliteConnection* connection,
    const char* sql,
    int size,
    HuxerSqliteStatement** statement,
    const char** tail
);
int huxer_sqlite_finalize(HuxerSqliteStatement* statement);
int huxer_sqlite_step(HuxerSqliteStatement* statement);
int huxer_sqlite_reset(HuxerSqliteStatement* statement);
int huxer_sqlite_clear_bindings(HuxerSqliteStatement* statement);
int huxer_sqlite_column_count(HuxerSqliteStatement* statement);
const char* huxer_sqlite_column_name(HuxerSqliteStatement* statement, int column);
int huxer_sqlite_column_type(HuxerSqliteStatement* statement, int column);
int64_t huxer_sqlite_column_int64(HuxerSqliteStatement* statement, int column);
double huxer_sqlite_column_double(HuxerSqliteStatement* statement, int column);
const unsigned char* huxer_sqlite_column_text(HuxerSqliteStatement* statement, int column);
const void* huxer_sqlite_column_blob(HuxerSqliteStatement* statement, int column);
int huxer_sqlite_column_bytes(HuxerSqliteStatement* statement, int column);
int huxer_sqlite_parameter_count(HuxerSqliteStatement* statement);
int huxer_sqlite_bind_null(HuxerSqliteStatement* statement, int index);
int huxer_sqlite_bind_int64(HuxerSqliteStatement* statement, int index, int64_t value);
int huxer_sqlite_bind_double(HuxerSqliteStatement* statement, int index, double value);
int huxer_sqlite_bind_int(HuxerSqliteStatement* statement, int index, int value);
int huxer_sqlite_bind_text(
    HuxerSqliteStatement* statement,
    int index,
    const char* value,
    uint64_t size
);
int huxer_sqlite_bind_blob(
    HuxerSqliteStatement* statement,
    int index,
    const void* value,
    uint64_t size
);

int64_t huxer_sqlite_changes(HuxerSqliteConnection* connection);
int64_t huxer_sqlite_last_insert_row_id(HuxerSqliteConnection* connection);
const char* huxer_sqlite_version(void);
const char* huxer_sqlite_source_id(void);
const char* huxer_sqlite_compile_option(int index);

#ifdef __cplusplus
}
#endif
