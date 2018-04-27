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
#include "svn_hash.h"
#include "svn_ctype.h"
#include "private/svn_dep_compat.h"

#include "svn_private_config.h"

#include "../svn_test.h"

#ifdef SVN_SQLITE_INLINE
/* Import the sqlite3 API vtable from sqlite3wrapper.c */
#  define SQLITE_OMIT_DEPRECATED
#  include <sqlite3ext.h>
extern const sqlite3_api_routines *const svn_sqlite3__api_funcs;
extern int (*const svn_sqlite3__api_initialize)(void);
extern int (*const svn_sqlite3__api_config)(int, ...);
#  define sqlite3_api svn_sqlite3__api_funcs
#  define sqlite3_initialize svn_sqlite3__api_initialize
#  define sqlite3_config svn_sqlite3__api_config
#else
#  include <sqlite3.h>
#endif

#include "../../libsvn_wc/wc-queries.h"

WC_QUERIES_SQL_DECLARE_STATEMENTS(wc_queries);
WC_QUERIES_SQL_DECLARE_STATEMENT_INFO(wc_query_info);

/* The first query after the normal wc queries */
#define STMT_SCHEMA_FIRST STMT_CREATE_SCHEMA

#define SQLITE_ERR(x) do                                         \
{                                                                \
  int sqlite_err__temp = (x);                                    \
  if (sqlite_err__temp != SQLITE_OK)                             \
    return svn_error_createf(SVN_ERR_SQLITE_ERROR,               \
                             NULL, "sqlite: %s",                 \
                             sqlite3_errmsg(sdb));               \
} while (0)

/* Schema creation statements fail during preparing when the table
   already exists, and must be evaluated before testing the
   queries. Statements above STMT_SCHEMA_FIRST only need to be
   included here when they need to be evaluated before testing the
   statements */
static const int schema_statements[] =
{
  /* Usual tables */
  STMT_CREATE_SCHEMA,
  STMT_INSTALL_SCHEMA_STATISTICS,
  /* Memory tables */
  STMT_CREATE_TARGETS_LIST,
  STMT_CREATE_CHANGELIST_LIST,
  STMT_CREATE_CHANGELIST_TRIGGER,
  STMT_CREATE_TARGET_PROP_CACHE,
  STMT_CREATE_REVERT_LIST,
  STMT_CREATE_DELETE_LIST,
  STMT_CREATE_UPDATE_MOVE_LIST,
  -1 /* final marker */
};

/* These statements currently trigger warnings. It would be nice if
   we could annotate these in wc-queries.sql */
static const int slow_statements[] =
{
  /* Operate on the entire WC */
  STMT_SELECT_ALL_NODES,                /* schema validation code */

  /* Updates all records for a repository (designed slow) */
  STMT_UPDATE_LOCK_REPOS_ID,

  /* Full temporary table read */
  STMT_INSERT_ACTUAL_EMPTIES,
  STMT_INSERT_ACTUAL_EMPTIES_FILES,
  STMT_SELECT_REVERT_LIST_RECURSIVE,
  STMT_SELECT_DELETE_LIST,
  STMT_SELECT_UPDATE_MOVE_LIST,
  STMT_FIND_REPOS_PATH_IN_WC,

  /* Designed as slow to avoid penalty on other queries */
  STMT_SELECT_UNREFERENCED_PRISTINES,

  /* Slow, but just if foreign keys are enabled:
   * STMT_DELETE_PRISTINE_IF_UNREFERENCED,
   */
  STMT_HAVE_STAT1_TABLE, /* Queries sqlite_master which has no index */

  -1 /* final marker */
};

/* Statements that just read the first record from a table,
   using the primary key. Specialized as different sqlite
   versions produce different results */
static const int primary_key_statements[] =
{
  /* Is there a record? ### Can we somehow check for LIMIT 1,
     and primary key instead of adding a list? */
  STMT_LOOK_FOR_WORK,
  STMT_SELECT_WORK_ITEM,

  -1 /* final marker */
};

/* Helper function to determine if a statement is in a list */
static svn_boolean_t
in_list(const int list[], int stmt_idx)
{
  int i;

  for (i = 0; list[i] != -1; i++)
    {
      if (list[i] == stmt_idx)
        return TRUE;
    }
  return FALSE;
}

/* Helpers to determine if a statement is in a common list */
#define is_slow_statement(stmt_idx) in_list(slow_statements, stmt_idx)
#define is_schema_statement(stmt_idx) \
    ((stmt_idx >= STMT_SCHEMA_FIRST) || in_list(schema_statements, stmt_idx))


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

/* Verify sqlite3 runtime version */
static svn_error_t *
test_sqlite_version(apr_pool_t *scratch_pool)
{
  printf("DBG: Using Sqlite %s\n", sqlite3_libversion());

  if (sqlite3_libversion_number() != SQLITE_VERSION_NUMBER)
    printf("DBG: Compiled against Sqlite %s\n", SQLITE_VERSION);

  if (sqlite3_libversion_number() < SQLITE_VERSION_NUMBER)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
            "Compiled against Sqlite %s (at runtime we have Sqlite %s)",
            SQLITE_VERSION, sqlite3_libversion());

#if !SQLITE_VERSION_AT_LEAST(3, 7, 9)
  return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
        "Sqlite upgrade recommended:\n"
        "****************************************************************\n"
        "*   Subversion needs at least SQLite 3.7.9 to work optimally   *\n"
        "*                                                              *\n"
        "* With older versions, at least some queries that are expected *\n"
        "* to be using an index are not. This makes some operations use *\n"
        "* every node in the working copy instead of just one.          *\n"
        "*                                                              *\n"
        "* While Subversion works correctly in this case, you may see   *\n"
        "* slowdowns of WELL MORE THAN 1000* in some cases!             *\n"
        "*                                                              *\n"
        "*                                                              *\n"
        "*                SQLITE UPGRADE RECOMMENDED                    *\n"
        "****************************************************************\n");
#else
  return SVN_NO_ERROR;
#endif
}

/* Parse all normal queries */
static svn_error_t *
test_parsable(apr_pool_t *scratch_pool)
{
  sqlite3 *sdb;
  int i;

  SVN_ERR(create_memory_db(&sdb, scratch_pool));

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
                                     "Preparing %s failed: %s\n%s",
                                     wc_query_info[i][0],
                                     sqlite3_errmsg(sdb),
                                     text);

          SQLITE_ERR(sqlite3_finalize(stmt));

          /* Continue after the current statement */
          text = tail;
        }
    }

  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return SVN_NO_ERROR;
}

/* Contains a parsed record from EXPLAIN QUERY PLAN */
struct explanation_item
{
  const char *operation;
  const char *table;
  const char *alias;
  svn_boolean_t scan;
  svn_boolean_t search;
  svn_boolean_t covered_by_index;
  svn_boolean_t primary_key;
  svn_boolean_t automatic_index;
  const char *index;
  const char *expressions;
  const char *expected;

  const char *compound_left;
  const char *compound_right;
  svn_boolean_t create_btree;

  int expression_vars;
  int expected_rows;
};

#define MATCH_TOKEN(x, y) (x && (strcmp(x, y) == 0))

/* Simple parser for the Sqlite textual explanation into an explanation_item.
   Writes "DBG:" lines when sqlite produces unexpected results. When no
   valid explanation_item can be parsed sets *PARSED_ITEM to NULL, otherwise
   to a valid result. */
static svn_error_t *
parse_explanation_item(struct explanation_item **parsed_item,
                       const char *text,
                       apr_pool_t *result_pool)
{
  struct explanation_item *item = apr_pcalloc(result_pool, sizeof(*item));
  char *token;
  char *last;
  char *tmp = apr_pstrdup(result_pool, text);
  const char *tmp_end = &tmp[strlen(tmp)];

  *parsed_item = NULL;

  item->operation = apr_strtok(tmp, " ", &last);

  if (!item->operation)
    {
      return SVN_NO_ERROR;
    }

  item->scan = MATCH_TOKEN(item->operation, "SCAN");

  if (item->scan || MATCH_TOKEN(item->operation, "SEARCH"))
    {
      item->search = TRUE; /* Search or scan */
      token = apr_strtok(NULL, " ", &last);

      if (MATCH_TOKEN(token, "TABLE"))
        {
          item->table = apr_strtok(NULL, " ", &last);
        }
      else if (MATCH_TOKEN(token, "SUBQUERY"))
        {
          item->table = apr_psprintf(result_pool, "SUBQUERY-%s",
                                     apr_strtok(NULL, " ", &last));
        }
      else
        {
          printf("DBG: Expected 'TABLE', got '%s' in '%s'\n", token, text);
          return SVN_NO_ERROR; /* Nothing to parse */
        }

      token = apr_strtok(NULL, " ", &last);

      /* Skip alias */
      if (MATCH_TOKEN(token, "AS"))
        {
          item->alias = apr_strtok(NULL, " ", &last);
          token = apr_strtok(NULL, " ", &last);
        }

      if (MATCH_TOKEN(token, "USING"))
        {
          token = apr_strtok(NULL, " ", &last);

          if (MATCH_TOKEN(token, "AUTOMATIC"))
            {
              /* Pain: A temporary index is created */
              item->automatic_index = TRUE;
              token = apr_strtok(NULL, " ", &last);
            }

          /* Handle COVERING */
          if (MATCH_TOKEN(token, "COVERING"))
            {
              /* Bonus: Query will be answered by just using the index */
              item->covered_by_index = TRUE;
              token = apr_strtok(NULL, " ", &last);
           }

          if (MATCH_TOKEN(token, "INDEX"))
            {
              item->index = apr_strtok(NULL, " ", &last);
            }
          else if (MATCH_TOKEN(token, "INTEGER"))
            {
              token = apr_strtok(NULL, " ", &last);
              if (!MATCH_TOKEN(token, "PRIMARY"))
                {
                  printf("DBG: Expected 'PRIMARY', got '%s' in '%s'\n",
                         token, text);
                  return SVN_NO_ERROR;
                }

              token = apr_strtok(NULL, " ", &last);
              if (!MATCH_TOKEN(token, "KEY"))
                {
                  printf("DBG: Expected 'KEY', got '%s' in '%s'\n",
                         token, text);
                  return SVN_NO_ERROR;
                }

              item->primary_key = TRUE;
            }
          else
            {
              printf("DBG: Expected 'INDEX' or 'PRIMARY', got '%s' in '%s'\n",
                     token, text);
              return SVN_NO_ERROR;
            }

          token = apr_strtok(NULL, " ", &last);
        }

      if (token && token[0] == '(' && token[1] != '~')
        {
          /* Undo the tokenization to switch parser rules */
          size_t token_len = strlen(token);

          if (token + token_len < tmp_end)
            token[token_len] = ' ';

          if (token[token_len] == '\0')
            last[-1] = ' ';

          token++; /* Skip the '(' */

          item->expressions = apr_strtok(token, ")", &last);
          token = apr_strtok(NULL, " ", &last);
        }

      if (token && *token == '(' && token[1] == '~')
        {
          /* Undo the tokenization to switch parser rules */
          size_t token_len = strlen(token);

          if (token + token_len < tmp_end)
            token[token_len] = ' ';

          if (token[token_len] == '\0')
            last[-1] = ' ';

          token += 2; /* Skip "(~" */

          item->expected = apr_strtok(token, ")", &last);
          token = apr_strtok(NULL, " ", &last);
        }

      if (token)
        {
          printf("DBG: Unexpected token '%s' in '%s'\n",
                 token, text);
          return SVN_NO_ERROR;
        }

      /* Parsing successful */
    }
  else if (MATCH_TOKEN(item->operation, "EXECUTE"))
    {
      /* Subquery handling */
      return SVN_NO_ERROR;
    }
  else if (MATCH_TOKEN(item->operation, "COMPOUND"))
    {
      /* Handling temporary table (E.g. UNION) */

      token = apr_strtok(NULL, " ", &last);
      if (!MATCH_TOKEN(token, "SUBQUERIES"))
        {
          printf("DBG: Expected 'SUBQUERIES', got '%s' in '%s'\n", token,
                 text);
          return SVN_NO_ERROR;
        }

      item->compound_left = apr_strtok(NULL, " ", &last);
      token = apr_strtok(NULL, " ", &last);

      if (!MATCH_TOKEN(token, "AND"))
        {
          printf("DBG: Expected 'AND', got '%s' in '%s'\n", token, text);
          return SVN_NO_ERROR;
        }

      item->compound_right = apr_strtok(NULL, " ", &last);

      token = apr_strtok(NULL, " ", &last);
      if (MATCH_TOKEN(token, "USING"))
        {
          token = apr_strtok(NULL, " ", &last);
          if (!MATCH_TOKEN(token, "TEMP"))
            {
              printf("DBG: Expected 'TEMP', got '%s' in '%s'\n", token, text);
            }
          token = apr_strtok(NULL, " ", &last);
          if (!MATCH_TOKEN(token, "B-TREE"))
            {
              printf("DBG: Expected 'B-TREE', got '%s' in '%s'\n", token,
                     text);
            }
          item->create_btree = TRUE;
        }
    }
  else if (MATCH_TOKEN(item->operation, "USE"))
    {
      /* Using a temporary table for ordering results */
      /* ### Need parsing */
      item->create_btree = TRUE;
    }
  else
    {
      printf("DBG: Unhandled sqlite operation '%s' in explanation\n", item->operation);
      return SVN_NO_ERROR;
    }

  if (item->expressions)
    {
      const char *p;

      for (p = item->expressions; *p; p++)
        {
          if (*p == '?')
            item->expression_vars++;
        }
    }
  if (item->expected)
    {
      item->expected_rows = atoi(item->expected);
    }

  *parsed_item = item;
  return SVN_NO_ERROR;
}

/* Sqlite has an SQLITE_OMIT_EXPLAIN compilation flag, which may make
   explain query just evaluate the query. Some older versions use a
   different number of columns (and different texts) for
   EXPLAIN query plan.

   If none of this is true set *SUPPORTED to TRUE, otherwise to FALSE */
static svn_error_t *
supported_explain_query_plan(svn_boolean_t *supported,
                             sqlite3 *sdb,
                             apr_pool_t *scratch_pool)
{
  sqlite3_stmt *stmt;
  int r;

  *supported = TRUE;

  r = sqlite3_prepare(sdb, "EXPLAIN QUERY PLAN SELECT 1",
                      -1, &stmt, NULL);

  if (r != SQLITE_OK)
    {
      *supported = FALSE;
      return SVN_NO_ERROR;
    }

  if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      if (sqlite3_column_count(stmt) < 4)
        {
          *supported = FALSE;
          /* Fall through */
        }
    }

  SQLITE_ERR(sqlite3_reset(stmt));
  SQLITE_ERR(sqlite3_finalize(stmt));
  return SVN_NO_ERROR;
}


/* Returns TRUE if TABLE_NAME specifies a nodes table, which should be indexed
   by wc_id and either local_relpath or parent_relpath */
static svn_boolean_t
is_node_table(const char *table_name)
{
  return (apr_strnatcasecmp(table_name, "nodes") == 0
          || apr_strnatcasecmp(table_name, "actual_node") == 0
          || apr_strnatcasecmp(table_name, "externals") == 0
          || apr_strnatcasecmp(table_name, "lock") == 0
          || apr_strnatcasecmp(table_name, "wc_lock") == 0
          || FALSE);
}

/* Returns TRUE if TABLE specifies an intermediate result table, which is
   allowed to have table scans, etc. */
static svn_boolean_t
is_result_table(const char *table_name)
{
  return (apr_strnatcasecmp(table_name, "target_prop_cache") == 0
          || apr_strnatcasecmp(table_name, "changelist_list") == 0
          || FALSE);
}

static svn_error_t *
test_query_expectations(apr_pool_t *scratch_pool)
{
  sqlite3 *sdb;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_error_t *warnings = NULL;
  svn_boolean_t supports_query_info;

  SVN_ERR(create_memory_db(&sdb, scratch_pool));

  SVN_ERR(supported_explain_query_plan(&supports_query_info, sdb,
                                       scratch_pool));
  if (!supports_query_info)
    {
      SQLITE_ERR(sqlite3_close(sdb));
      return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                              "Sqlite doesn't support EXPLAIN QUERY PLAN");
    }

  for (i=0; i < STMT_SCHEMA_FIRST; i++)
    {
      sqlite3_stmt *stmt;
      const char *tail;
      int r;
      svn_boolean_t warned = FALSE;
      apr_array_header_t *rows = NULL;

      if (is_schema_statement(i))
        continue;

      /* Prepare statement to find if it is a single statement. */
      r = sqlite3_prepare_v2(sdb, wc_queries[i], -1, &stmt, &tail);

      if (r != SQLITE_OK)
        continue; /* Parse failure is already reported by 'test_parable' */

      SQLITE_ERR(sqlite3_finalize(stmt));
      if (tail[0] != '\0')
        continue; /* Multi-queries are currently not testable */

      svn_pool_clear(iterpool);

      r = sqlite3_prepare_v2(sdb,
                             apr_pstrcat(iterpool,
                                         "EXPLAIN QUERY PLAN ",
                                         wc_queries[i],
                                         SVN_VA_NULL),
                             -1, &stmt, &tail);

      if (r != SQLITE_OK)
        continue; /* EXPLAIN not enabled or doesn't support this query */

      while (SQLITE_ROW == (r = sqlite3_step(stmt)))
        {
          /*int iSelectid;
          int iOrder;
          int iFrom;*/
          const unsigned char *zDetail;
          char *detail;
          struct explanation_item *item;

          /* ### The following code is correct for current Sqlite versions
             ### (tested with 3.7.x), but the EXPLAIN QUERY PLAN output
             ### is not guaranteed to be stable for future versions. */

          /* Names as in Sqlite documentation */
          /*iSelectid = sqlite3_column_int(stmt, 0);
          iOrder = sqlite3_column_int(stmt, 1);
          iFrom = sqlite3_column_int(stmt, 2);*/
          zDetail = sqlite3_column_text(stmt, 3);

          if (! zDetail)
            continue;

          if (!rows)
            rows = apr_array_make(iterpool, 10, sizeof(const char*));

          detail = apr_pstrdup(iterpool, (const char*)zDetail);

          APR_ARRAY_PUSH(rows, const char *) = detail;

          SVN_ERR(parse_explanation_item(&item, detail, iterpool));

          if (!item)
            continue; /* Not parsable or not interesting */

          if (item->search
              && item->automatic_index)
            {
              warned = TRUE;
              if (!is_slow_statement(i))
                {
                  warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                                "%s: "
                                "Creates a temporary index: %s\n",
                                wc_query_info[i][0], wc_queries[i]);
                }
            }
          else if (item->search && item->primary_key)
            {
              /* Nice */
            }
          else if (item->search
                   && ((item->expression_vars < 2 && is_node_table(item->table))
                       || (item->expression_vars < 1))
                   && !is_result_table(item->table))
            {
              if (in_list(primary_key_statements, i))
                {
                  /* Reported as primary key index usage in Sqlite 3.7,
                     as table scan in 3.8+, while the execution plan is
                     identical: read first record from table */
                }
              else if (!is_slow_statement(i))
                {
                  warned = TRUE;
                  warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                                "%s: "
                                "Uses %s with only %d index component: (%s)\n%s",
                                wc_query_info[i][0], item->table,
                                item->expression_vars, item->expressions,
                                wc_queries[i]);
                }
              else
                warned = TRUE;
            }
          else if (item->search && !item->index)
            {
              warned = TRUE;
              if (!is_slow_statement(i))
                warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                                "%s: "
                                "Query on %s doesn't use an index:\n%s",
                                wc_query_info[i][0], item->table, wc_queries[i]);
            }
          else if (item->scan && !is_result_table(item->table))
            {
              warned = TRUE;
              if (!is_slow_statement(i))
                warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                                "Query %s: "
                                "Performs scan on %s:\n%s",
                                wc_query_info[i][0], item->table, wc_queries[i]);
            }
          else if (item->create_btree)
            {
              warned = TRUE;
              if (!is_slow_statement(i))
                warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                                "Query %s: Creates a temporary B-TREE:\n%s",
                                wc_query_info[i][0], wc_queries[i]);
            }
        }
      SQLITE_ERR(sqlite3_reset(stmt));
      SQLITE_ERR(sqlite3_finalize(stmt));

      if (!warned && is_slow_statement(i))
        {
          printf("DBG: Expected %s to be reported as slow, but it wasn't\n",
                 wc_query_info[i][0]);
        }

      if (rows && warned != is_slow_statement(i))
        {
          int w;
          svn_error_t *info = NULL;
          for (w = rows->nelts-1; w >= 0; w--)
            {
              if (warned)
                info = svn_error_createf(SVN_ERR_SQLITE_CONSTRAINT, info,
                                         "|%s", APR_ARRAY_IDX(rows, w,
                                                              const char*));
              else
                printf("|%s\n", APR_ARRAY_IDX(rows, w, const char*));
            }

          warnings = svn_error_compose_create(warnings, info);
        }
    }
  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return warnings;
}

static svn_error_t *
test_query_duplicates(apr_pool_t *scratch_pool)
{
  sqlite3 *sdb;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_error_t *warnings = NULL;
  svn_boolean_t supports_query_info;
  apr_hash_t *sha_to_query = apr_hash_make(scratch_pool);

  SVN_ERR(create_memory_db(&sdb, scratch_pool));

  SVN_ERR(supported_explain_query_plan(&supports_query_info, sdb,
      scratch_pool));
  if (!supports_query_info)
    {
      SQLITE_ERR(sqlite3_close(sdb));
      return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
          "Sqlite doesn't support EXPLAIN QUERY PLAN");
    }

  for (i = 0; i < STMT_SCHEMA_FIRST; i++)
    {
      sqlite3_stmt *stmt;
      const char *tail;
      int r;
      svn_stringbuf_t *result;
      svn_checksum_t *checksum;

      if (is_schema_statement(i))
        continue;

      /* Prepare statement to find if it is a single statement. */
      r = sqlite3_prepare_v2(sdb, wc_queries[i], -1, &stmt, &tail);

      if (r != SQLITE_OK)
        continue; /* Parse failure is already reported by 'test_parable' */

      SQLITE_ERR(sqlite3_finalize(stmt));
      if (tail[0] != '\0')
        continue; /* Multi-queries are currently not testable */

      svn_pool_clear(iterpool);

      r = sqlite3_prepare_v2(sdb,
                             apr_pstrcat(iterpool,
                             "EXPLAIN ",
                             wc_queries[i],
                             SVN_VA_NULL),
                             -1, &stmt, &tail);

      if (r != SQLITE_OK)
          continue; /* EXPLAIN not enabled or doesn't support this query */

      result = svn_stringbuf_create_empty(iterpool);

      while (SQLITE_ROW == (r = sqlite3_step(stmt)))
        {
          int col;

          for (col = 0; col < sqlite3_column_count(stmt); col++)
            {
              const char *txt = (const char*)sqlite3_column_text(stmt, col);
              if (txt)
                  svn_stringbuf_appendcstr(result, txt);

              svn_stringbuf_appendcstr(result, "|");
            }

          svn_stringbuf_appendcstr(result, "\n");
        }

      SQLITE_ERR(sqlite3_reset(stmt));
      SQLITE_ERR(sqlite3_finalize(stmt));

      SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1,
                           result->data, result->len,
                           iterpool));

      {
        const char *hex = svn_checksum_to_cstring(checksum, scratch_pool);
        const char *other;

        other = svn_hash_gets(sha_to_query, hex);
        if (other)
          {
            warnings = svn_error_createf(SVN_ERR_TEST_FAILED, warnings,
                              "Query %s has an identical execution plan as %s",
                              wc_query_info[i][0], other);
          }
        else
          svn_hash_sets(sha_to_query, hex, wc_query_info[i][0]);
      }
    }
  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return warnings;
}

/* Helper to verify a bit of data in the sqlite3 statistics */
static int
parse_stat_data(const char *stat)
{
  int n = 0;
  apr_int64_t last = APR_INT64_MAX;
  while (*stat)
    {
      apr_int64_t v;
      char *next;

      if (*stat < '0' || *stat > '9')
        return -2;

      errno = 0;
      v = apr_strtoi64(stat, &next, 10);

      /* All numbers specify the average number of rows
         with the same values in all columns left of it,
         so the value must be >= 1 and lower than or equal
         to all previous seen numbers */
      if (v <= 0 || (v > last) || (errno != 0))
        return -1;

      last = v;

      n++;
      stat = next;

      if (*stat == ' ')
        stat++;
    }

  return n;
}

static svn_error_t *
test_schema_statistics(apr_pool_t *scratch_pool)
{
  sqlite3 *sdb;
  sqlite3_stmt *stmt;

  SVN_ERR(create_memory_db(&sdb, scratch_pool));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "CREATE TABLE shadow_stat1(tbl TEXT, idx TEXT, stat TEXT)",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO shadow_stat1 (tbl, idx, stat) "
                   "SELECT tbl, idx, stat FROM sqlite_stat1",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "DROP TABLE sqlite_stat1",
                   NULL, NULL, NULL));

  /* Insert statement to give index at least 1 record */
  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO nodes (wc_id, local_relpath, op_depth,"
                   "                   presence, kind) "
                   "VALUES (1, '', 0, 'normal', 'dir')",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO actual_node (wc_id, local_relpath) "
                   "VALUES (1, '')",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO lock (repos_id, repos_relpath, lock_token) "
                   "VALUES (1, '', '')",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO EXTERNALS (wc_id, local_relpath,"
                   "                       parent_relpath, repos_id,"
                   "                       presence, kind, def_local_relpath,"
                   "                       def_repos_relpath) "
                   "VALUES (1, 'subdir', '', 1, 'normal', 'dir', '', '')",
                   NULL, NULL, NULL));

  /* These are currently not necessary for query optimization, but it's better
     to tell Sqlite how we intend to use this table anyway */
  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO wc_lock (wc_id, local_dir_relpath) "
                   "VALUES (1, '')",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "INSERT INTO WORK_QUEUE (work) "
                   "VALUES ('')",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_exec(sdb,
                   "ANALYZE",
                   NULL, NULL, NULL));

  SQLITE_ERR(
      sqlite3_prepare(sdb, "SELECT s.tbl, s.idx, s.stat, r.stat "
                           "FROM shadow_stat1 s "
                           "LEFT JOIN sqlite_stat1 r ON "
                                "s.tbl=r.tbl and s.idx=r.idx",
                      -1, &stmt, NULL));

  while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      const char *wc_stat       = (const char*)sqlite3_column_text(stmt, 2);
      const char *sqlite_stat   = (const char*)sqlite3_column_text(stmt, 3);

      if (! sqlite_stat)
        {
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Schema statistic failure:"
                                   " Refering to unknown index '%s' on '%s'",
                                   sqlite3_column_text(stmt, 1),
                                   sqlite3_column_text(stmt, 0));
        }

      if (parse_stat_data(wc_stat) != parse_stat_data(sqlite_stat))
        {
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Schema statistic failure:"
                                   " Column mismatch for '%s' on '%s'",
                                   sqlite3_column_text(stmt, 1),
                                   sqlite3_column_text(stmt, 0));
        }
    }

  SQLITE_ERR(sqlite3_reset(stmt));
  SQLITE_ERR(sqlite3_finalize(stmt));

  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return SVN_NO_ERROR;
}

/* An SQLite application defined function that allows SQL queries to
   use "relpath_depth(local_relpath)".  */
static void relpath_depth_sqlite(sqlite3_context* context,
                                 int argc,
                                 sqlite3_value* values[])
{
  SVN_ERR_MALFUNCTION_NO_RETURN(); /* STUB! */
}

/* Parse all verify/check queries */
static svn_error_t *
test_verify_parsable(apr_pool_t *scratch_pool)
{
  sqlite3 *sdb;
  int i;

  SVN_ERR(create_memory_db(&sdb, scratch_pool));

  SQLITE_ERR(sqlite3_create_function(sdb, "relpath_depth", 1, SQLITE_ANY, NULL,
                                     relpath_depth_sqlite, NULL, NULL));

  for (i=STMT_VERIFICATION_TRIGGERS; wc_queries[i]; i++)
    {
      sqlite3_stmt *stmt;
      const char *text = wc_queries[i];

      /* Some of our statement texts contain multiple queries. We prepare
         them all. */
      while (*text != '\0')
        {
          const char *tail;
          int r = sqlite3_prepare_v2(sdb, text, -1, &stmt, &tail);

          if (r != SQLITE_OK)
            return svn_error_createf(SVN_ERR_SQLITE_ERROR, NULL,
                                     "Preparing %s failed: %s\n%s",
                                     wc_query_info[i][0],
                                     sqlite3_errmsg(sdb),
                                     text);

          SQLITE_ERR(sqlite3_finalize(stmt));

          /* Continue after the current statement */
          text = tail;
        }
    }

  SQLITE_ERR(sqlite3_close(sdb)); /* Close the DB if ok; otherwise leaked */

  return SVN_NO_ERROR;
}


static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_sqlite_version,
                   "sqlite up-to-date"),
    SVN_TEST_PASS2(test_parsable,
                   "queries are parsable"),
    SVN_TEST_PASS2(test_query_expectations,
                   "test query expectations"),
    SVN_TEST_PASS2(test_query_duplicates,
                   "test query duplicates"),
    SVN_TEST_PASS2(test_schema_statistics,
                   "test schema statistics"),
    SVN_TEST_PASS2(test_verify_parsable,
                   "verify queries are parsable"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
