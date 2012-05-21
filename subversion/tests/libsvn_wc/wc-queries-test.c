/*
 * wc-queries-test.c -- test the evaluation of the wc Sqlite queries
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "svn_pools.h"
#include "svn_ctype.h"

#include "../svn_test.h"

#ifdef SVN_SQLITE_INLINE
/* Include sqlite3 inline, making all symbols private. */
  #define SQLITE_API static
  #include <sqlite3.c>
#else
  #include <sqlite3.h>
#endif

#include "../../libsvn_wc/wc-queries.h"

WC_QUERIES_SQL_DECLARE_STATEMENTS(wc_queries);

/* The first query after the normal wc queries */
#define STMT_SCHEMA_FIRST STMT_CREATE_SCHEMA

#define SQLITE_ERR(x)   \
{                                                                \
  int sqlite_err__temp = (x);                                    \
  if (sqlite_err__temp != SQLITE_OK)                             \
    return svn_error_createf(SVN_ERR_SQLITE_ERROR,               \
                             NULL, "sqlite: %s",                 \
                             sqlite3_errmsg(sdb));               \
} while (0)

static const int schema_statements[] =
{
  /* Usual tables */
  STMT_CREATE_SCHEMA,
  STMT_CREATE_NODES,
  STMT_CREATE_NODES_TRIGGERS,
  STMT_CREATE_EXTERNALS,
  /* Memory tables */
  STMT_CREATE_TARGETS_LIST,
  STMT_CREATE_CHANGELIST_LIST,
  STMT_CREATE_NODE_PROPS_CACHE,
  STMT_CREATE_REVERT_LIST,
  STMT_CREATE_DELETE_LIST,
  -1 /* final marker */
};

/* Schema creation statements fail during preparing when the table
   already exists */

static svn_boolean_t
is_schema_statement(int stmt_idx)
{
  int i;

  if (stmt_idx >= STMT_SCHEMA_FIRST)
    return TRUE;

  for (i = 0; schema_statements[i] != -1; i++)
    {
      if (schema_statements[i] == stmt_idx)
        return TRUE;
    }
  return FALSE;
}

/* Create an in-memory db for evaluating queries */
static svn_error_t *
create_memory_db(sqlite3 **db,
                 apr_pool_t *pool)
{
  sqlite3 *sdb;
  int i;

  /* Create an in-memory raw database */
  SVN_TEST_ASSERT(sqlite3_initialize() == SQLITE_OK);
  SQLITE_ERR(sqlite3_open_v2("", &sdb,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             NULL));

  /* Create schema */
  for (i = 0; schema_statements[i] != -1; i++)
    {
      SQLITE_ERR(sqlite3_exec(sdb, wc_queries[schema_statements[i]], NULL, NULL, NULL));
    }

  *db = sdb;
  return SVN_NO_ERROR;
}

/* Parse all normal queries */
static svn_error_t *
test_parsable(apr_pool_t *pool)
{
  sqlite3 *sdb;
  int i;

  SVN_ERR(create_memory_db(&sdb, pool));

  for (i=0; i < STMT_SCHEMA_FIRST; i++)
    {
      sqlite3_stmt *stmt;
      const char *text = wc_queries[i];

      if (is_schema_statement(i))
        continue;

      /* Some of our statement texts contain multiple queries. We prepare
         them all. */
      while (*text != '\0')
        {
          const char *tail;
          int r = sqlite3_prepare_v2(sdb, text, -1, &stmt, &tail);

          if (r != SQLITE_OK)
            return svn_error_createf(SVN_ERR_SQLITE_ERROR, NULL,
                                     "Preparing statement %d failed: %s\n%s",
                                     i, sqlite3_errmsg(sdb),
                                     text);

          SQLITE_ERR(sqlite3_finalize(stmt));

          /* Continue after the current statement */
          text = tail;
        }
    }

  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return SVN_NO_ERROR;
}

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_parsable,
                   "queries are parsable"),
    SVN_TEST_NULL
  };
