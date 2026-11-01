#pragma once

// Minimal libpq C API declarations.
//
// The deployment image installs libpqxx/libpq-dev, but the development sandbox
// used for this rewrite only ships the libpq runtime library. Declaring the
// small surface we need keeps the build self-contained without vendoring the
// PostgreSQL headers.
//
// These declarations match the stable libpq ABI (PostgreSQL 12+). The opaque
// PGconn/PGresult types are never dereferenced.

#include <cstddef>

extern "C" {

typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;
typedef unsigned int Oid;

enum {
    CONNECTION_OK = 0,
    CONNECTION_BAD = 1,
    CONNECTION_STARTED = 2,
    CONNECTION_MADE = 3,
    CONNECTION_AWAITING_RESPONSE = 4,
    CONNECTION_AUTH_OK = 5,
    CONNECTION_SETENV = 6,
    CONNECTION_SSL_STARTUP = 7,
    CONNECTION_NEEDED = 8,
    CONNECTION_CHECK_WRITABLE = 9,
    CONNECTION_CONSUME = 10,
    CONNECTION_GSS_STARTUP = 11,
    CONNECTION_CHECK_TARGET = 12,
    CONNECTION_CHECK_STANDBY = 13
};

enum {
    PGRES_EMPTY_QUERY = 0,
    PGRES_COMMAND_OK = 1,
    PGRES_TUPLES_OK = 2,
    PGRES_COPY_OUT = 3,
    PGRES_COPY_IN = 4,
    PGRES_BAD_RESPONSE = 5,
    PGRES_NONFATAL_ERROR = 6,
    PGRES_FATAL_ERROR = 7,
    PGRES_COPY_BOTH = 8,
    PGRES_SINGLE_TUPLE = 9,
    PGRES_PIPELINE_SYNC = 10,
    PGRES_PIPELINE_ABORTED = 11
};

PGconn* PQconnectdb(const char* conninfo);
int PQstatus(const PGconn* conn);
char* PQerrorMessage(const PGconn* conn);
void PQfinish(PGconn* conn);

PGresult* PQexec(PGconn* conn, const char* query);
PGresult* PQexecParams(PGconn* conn,
                       const char* command,
                       int nParams,
                       const Oid* paramTypes,
                       const char* const* paramValues,
                       const int* paramLengths,
                       const int* paramFormats,
                       int resultFormat);

int PQresultStatus(const PGresult* res);
char* PQresultErrorMessage(const PGresult* res);
char* PQresultErrorField(const PGresult* res, int fieldcode);
int PQntuples(const PGresult* res);
int PQnfields(const PGresult* res);
char* PQfname(const PGresult* res, int field_num);
char* PQgetvalue(const PGresult* res, int tup_num, int field_num);
int PQgetisnull(const PGresult* res, int tup_num, int field_num);
char* PQcmdTuples(PGresult* res);
void PQclear(PGresult* res);

int PQsetClientEncoding(PGconn* conn, const char* encoding);
char* PQdb(const PGconn* conn);
char* PQuser(const PGconn* conn);

} // extern "C"
