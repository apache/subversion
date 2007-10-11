/* merge-info-sqlite-index.c
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <apr_general.h>
#include <apr_pools.h>

#include <sqlite3.h>

#include "svn_fs.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"

#include "private/svn_compat.h"
#include "private/svn_fs_mergeinfo.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

/* This is a macro implementation of svn_fs_revision_root_revision(), which
   we cannot call from here, because it would create a circular dependency. */
#define REV_ROOT_REV(root)       \
  ((root)->is_txn_root? SVN_INVALID_REVNUM : (root)->rev)

/* We want to cache that we saw no mergeinfo for a path as well,
   so we use a -1 converted to a pointer to represent this. */
#define NEGATIVE_CACHE_RESULT ((void *)(-1))

/* SQLITE->SVN quick error wrap, much like SVN_ERR. */
#define SQLITE_ERR(x, db) do                                    \
{                                                               \
  if ((x) != SQLITE_OK)                                         \
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,      \
                            sqlite3_errmsg((db)));              \
} while (0)

/* A flow-control helper macro for sending processing to the 'cleanup'
  label when the local variable 'err' is not SVN_NO_ERROR. */
#define MAYBE_CLEANUP if (err) goto cleanup

#ifdef SQLITE3_DEBUG
/* An sqlite query execution callback. */
static void
sqlite_tracer(void *data, const char *sql)
{
  /*  sqlite3 *db = data; */
  fprintf(stderr, "SQLITE SQL is \"%s\"\n", sql);
}
#endif

/* Execute SQL on the sqlite database DB, and raise an SVN error if the
   result is not okay.  */

static svn_error_t *
util_sqlite_exec(sqlite3 *db, const char *sql,
                 sqlite3_callback callback,
                 void *callbackdata)
{
  char *err_msg;
  svn_error_t *err;
  if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK)
    {
      err = svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL, err_msg);
      sqlite3_free(err_msg);
      return err;
    }
  return SVN_NO_ERROR;
}

/* Time (in milliseconds) to wait for sqlite locks before giving up. */
#define BUSY_TIMEOUT 10000

/* The version number of the schema used to store the mergeinfo index. */
#define MERGE_INFO_INDEX_SCHEMA_FORMAT 1

/* Return SVN_ERR_FS_GENERAL if the schema doesn't exist,
   SVN_ERR_FS_UNSUPPORTED_FORMAT if the schema format is invalid, or
   SVN_ERR_FS_SQLITE_ERROR if an sqlite error occurs during
   validation.  Return SVN_NO_ERROR if everything is okay. */
static svn_error_t *
check_format(sqlite3 *db)
{
  svn_error_t *err = SVN_NO_ERROR;
  sqlite3_stmt *stmt;

  SQLITE_ERR(sqlite3_prepare(db, "PRAGMA user_version;", -1, &stmt, NULL), db);
  if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      /* Validate that the schema exists as expected and that the
         schema and repository versions match. */
      int schema_format = sqlite3_column_int(stmt, 0);
      if (schema_format == MERGE_INFO_INDEX_SCHEMA_FORMAT)
        {
          err = SVN_NO_ERROR;
        }
      else if (schema_format == 0)
        {
          /* This is likely a freshly-created database in which the
             merge tracking schema doesn't yet exist. */
          err = svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                                 _("Merge Tracking schema format not set"));
        }
      else if (schema_format > MERGE_INFO_INDEX_SCHEMA_FORMAT)
        {
          err = svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                                  _("Merge Tracking schema format %d "
                                    "not recognized"), schema_format);
        }
      /* else, we may one day want to perform a schema migration. */

      SQLITE_ERR(sqlite3_finalize(stmt), db);
    }
  else
    {
      err = svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                             sqlite3_errmsg(db));
    }
  return err;
}

const char SVN_MTD_CREATE_SQL[] = "PRAGMA auto_vacuum = 1;"
  APR_EOL_STR
  "CREATE TABLE mergeinfo (revision INTEGER NOT NULL, mergedfrom TEXT NOT "
  "NULL, mergedto TEXT NOT NULL, mergedrevstart INTEGER NOT NULL, "
  "mergedrevend INTEGER NOT NULL, inheritable INTEGER NOT NULL);"
  APR_EOL_STR
  "CREATE INDEX mi_mergedfrom_idx ON mergeinfo (mergedfrom);"
  APR_EOL_STR
  "CREATE INDEX mi_mergedto_idx ON mergeinfo (mergedto);"
  APR_EOL_STR
  "CREATE INDEX mi_revision_idx ON mergeinfo (revision);"
  APR_EOL_STR
  "CREATE TABLE mergeinfo_changed (revision INTEGER NOT NULL, path TEXT "
  "NOT NULL);"
  APR_EOL_STR
  "CREATE UNIQUE INDEX mi_c_revpath_idx ON mergeinfo_changed (revision, path);"
  APR_EOL_STR
  "CREATE INDEX mi_c_path_idx ON mergeinfo_changed (path);"
  APR_EOL_STR
  "CREATE INDEX mi_c_revision_idx ON mergeinfo_changed (revision);"
  APR_EOL_STR
  "PRAGMA user_version = " APR_STRINGIFY(MERGE_INFO_INDEX_SCHEMA_FORMAT) ";"
  APR_EOL_STR;

/* Open a connection in *DB to the mergeinfo database under
   REPOS_PATH.  Validate the merge tracking schema, creating it if it
   doesn't yet exist.  This provides a migration path for pre-1.5
   repositories. */
static svn_error_t *
open_db(sqlite3 **db, const char *repos_path, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *db_path = svn_path_join(repos_path,
                                      SVN_FS_MERGEINFO__DB_NAME, pool);
  SQLITE_ERR(sqlite3_open(db_path, db), *db);
  /* Retry until timeout when database is busy. */
  SQLITE_ERR(sqlite3_busy_timeout(*db, BUSY_TIMEOUT), *db);
#ifdef SQLITE3_DEBUG
  sqlite3_trace(*db, sqlite_tracer, *db);
#endif

  /* Validate the schema. */
  err = check_format(*db);
  if (err && err->apr_err == SVN_ERR_FS_GENERAL)
    {
      /* Assume that we've just created an empty mergeinfo index by
         way of sqlite3_open() (likely from accessing a pre-1.5
         repository), and need to create the merge tracking schema. */
      svn_error_clear(err);
      err = util_sqlite_exec(*db, SVN_MTD_CREATE_SQL, NULL, NULL);
    }
  return err;
}

/* Close DB, returning any ERR which may've necessitated an early connection
   closure, or -- if none -- the error from the closure itself. */
static svn_error_t *
close_db(sqlite3 *db, svn_error_t *err)
{
  int result = sqlite3_close(db);
  /* If there's a pre-existing error, return it. */
  /* ### If the connection close also fails, say something about it as well? */
  SVN_ERR(err);
  SQLITE_ERR(result, db);
  return SVN_NO_ERROR;
}

/* Create an sqlite DB for our mergeinfo index under PATH.  Use POOL
   for temporary allocations. */
svn_error_t *
svn_fs_mergeinfo__create_index(const char *path, apr_pool_t *pool)
{
  sqlite3 *db;
  /* Opening the database will create it + schema if it's not already there. */
  SVN_ERR(open_db(&db, path, pool));
  return close_db(db, SVN_NO_ERROR);
}

static svn_error_t *
get_mergeinfo_for_path(sqlite3 *db,
                       const char *path,
                       svn_revnum_t rev,
                       apr_hash_t *result,
                       apr_hash_t *cache,
                       svn_mergeinfo_inheritance_t inherit,
                       apr_pool_t *pool);

/* Represents "no mergeinfo". */
static svn_merge_range_t no_mergeinfo = { SVN_INVALID_REVNUM,
                                          SVN_INVALID_REVNUM };

/* Insert the necessary indexing data into the DB for all the merges
   on PATH as of NEW_REV, which is provided (unparsed) in
   MERGEINFO_STR.  Use POOL for temporary allocations.*/
static svn_error_t *
index_path_mergeinfo(svn_revnum_t new_rev,
                     sqlite3 *db,
                     const char *path,
                     svn_string_t *mergeinfo_str,
                     apr_pool_t *pool)
{
  apr_hash_t *mergeinfo;
  apr_hash_index_t *hi;
  sqlite3_stmt *stmt;
  svn_boolean_t remove_mergeinfo = FALSE;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, mergeinfo_str->data, pool));

  if (apr_hash_count(mergeinfo) == 0)
    {
      /* All mergeinfo has been removed from PATH (or explicitly set
         to "none", if there previously was no mergeinfo).  Find all
         previous mergeinfo, and (further below) insert dummy records
         representing "no mergeinfo" for all its previous merge
         sources of PATH. */
      apr_hash_t *cache = apr_hash_make(pool);
      remove_mergeinfo = TRUE;
      SVN_ERR(get_mergeinfo_for_path(db, path, new_rev, mergeinfo, cache,
                                     svn_mergeinfo_inherited, pool));
      mergeinfo = apr_hash_get(mergeinfo, path, APR_HASH_KEY_STRING);
      if (mergeinfo == NULL)
        /* There was previously no mergeinfo, inherited or explicit,
           for PATH. */
        return SVN_NO_ERROR;
    }

  for (hi = apr_hash_first(NULL, mergeinfo);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *from;
      apr_array_header_t *rangelist;
      const void *key;
      void *val;

      apr_hash_this(hi, &key, NULL, &val);

      from = key;
      rangelist = val;
      if (from && rangelist)
        {
          int i;
          SQLITE_ERR(sqlite3_prepare
                     (db,
                      "INSERT INTO mergeinfo (revision, mergedfrom, "
                      "mergedto, mergedrevstart, mergedrevend, inheritable) "
                      "VALUES (?, ?, ?, ?, ?, ?);",
                      -1, &stmt, NULL), db);
          SQLITE_ERR(sqlite3_bind_int64(stmt, 1, new_rev), db);
          SQLITE_ERR(sqlite3_bind_text(stmt, 2, from, -1, SQLITE_TRANSIENT),
                     db);
          SQLITE_ERR(sqlite3_bind_text(stmt, 3, path, -1, SQLITE_TRANSIENT),
                     db);

          if (remove_mergeinfo)
            {
              /* Explicitly set "no mergeinfo" for PATH, which may've
                 previously had only inherited mergeinfo. */
#if APR_VERSION_AT_LEAST(1, 3, 0)
              apr_array_clear(rangelist);
#else
              rangelist = apr_array_make(pool, 1, sizeof(&no_mergeinfo));
#endif
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = &no_mergeinfo;
            }

          for (i = 0; i < rangelist->nelts; i++)
            {
              const svn_merge_range_t *range =
                APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
              SQLITE_ERR(sqlite3_bind_int64(stmt, 4, range->start),
                         db);
              SQLITE_ERR(sqlite3_bind_int64(stmt, 5, range->end),
                         db);
              SQLITE_ERR(sqlite3_bind_int64(stmt, 6, range->inheritable), db);
              if (sqlite3_step(stmt) != SQLITE_DONE)
                return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                        sqlite3_errmsg(db));

              SQLITE_ERR(sqlite3_reset(stmt), db);
            }
          SQLITE_ERR(sqlite3_finalize(stmt), db);
        }
    }
  SQLITE_ERR(sqlite3_prepare(db,
                             "INSERT INTO mergeinfo_changed (revision, path) "
                             "VALUES (?, ?);", -1, &stmt, NULL),
             db);
  SQLITE_ERR(sqlite3_bind_int64(stmt, 1, new_rev), db);

  SQLITE_ERR(sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT),
             db);

  if (sqlite3_step(stmt) != SQLITE_DONE)
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            sqlite3_errmsg(db));

  SQLITE_ERR(sqlite3_finalize(stmt), db);

  return SVN_NO_ERROR;
}


/* Index the mergeinfo for each path in MERGEINFO_FOR_PATHS (a
   mapping of const char * -> to svn_string_t *). */
static svn_error_t *
index_txn_mergeinfo(sqlite3 *db,
                    svn_revnum_t new_rev,
                    apr_hash_t *mergeinfo_for_paths,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, mergeinfo_for_paths);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *path;
      void *mergeinfo;

      apr_hash_this(hi, &path, NULL, &mergeinfo);
      SVN_ERR(index_path_mergeinfo(new_rev, db, (const char *) path,
                                    (svn_string_t *) mergeinfo, pool));
    }
  return SVN_NO_ERROR;
}

/* Clean the mergeinfo index for any previous failed commit with the
   revision number as NEW_REV, and if the current transaction contains
   mergeinfo, record it. */
svn_error_t *
svn_fs_mergeinfo__update_index(svn_fs_txn_t *txn, svn_revnum_t new_rev,
                               apr_hash_t *mergeinfo_for_paths,
                               apr_pool_t *pool)
{
  svn_error_t *err;
  sqlite3 *db;
  const char *deletestring;

  SVN_ERR(open_db(&db, txn->fs->path, pool));
  err = util_sqlite_exec(db, "BEGIN TRANSACTION;", NULL, NULL);
  MAYBE_CLEANUP;

  /* Cleanup the leftovers of any previous, failed transactions
   * involving NEW_REV. */
  deletestring = apr_psprintf(pool,
                              "DELETE FROM mergeinfo_changed WHERE "
                              "revision = %ld;",
                              new_rev);
  err = util_sqlite_exec(db, deletestring, NULL, NULL);
  MAYBE_CLEANUP;
  deletestring = apr_psprintf(pool,
                              "DELETE FROM mergeinfo WHERE revision = %ld;",
                              new_rev);
  err = util_sqlite_exec(db, deletestring, NULL, NULL);
  MAYBE_CLEANUP;

  /* Record any mergeinfo from the current transaction. */
  if (mergeinfo_for_paths)
    {
      err = index_txn_mergeinfo(db, new_rev, mergeinfo_for_paths, pool);
      MAYBE_CLEANUP;
    }

  /* This is moved here from FSFS's commit_txn, because we don't want to
   * write the final current file if the sqlite commit fails.
   * On the other hand, if we commit the transaction and end up failing
   * the current file, we just end up with inaccessible data in the
   * database, not a real problem.  */
  err = util_sqlite_exec(db, "COMMIT TRANSACTION;", NULL, NULL);
  MAYBE_CLEANUP;

 cleanup:
  return close_db(db, err);
}

/* Helper for get_mergeinfo_for_path() that retrieves mergeinfo for
   PATH at the revision LASTMERGED_REV, returning it in the merge
   info hash *RESULT (with rangelist elements in ascending order). */
static svn_error_t *
parse_mergeinfo_from_db(sqlite3 *db,
                        const char *path,
                        svn_revnum_t lastmerged_rev,
                        apr_hash_t **result,
                        apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;

  SQLITE_ERR(sqlite3_prepare(db,
                             "SELECT mergedfrom, mergedrevstart, "
                             "mergedrevend, inheritable FROM mergeinfo "
                             "WHERE mergedto = ? AND revision = ? "
                             "ORDER BY mergedfrom, mergedrevstart;",
                             -1, &stmt, NULL), db);
  SQLITE_ERR(sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT), db);
  SQLITE_ERR(sqlite3_bind_int64(stmt, 2, lastmerged_rev), db);
  sqlite_result = sqlite3_step(stmt);

  /* It is possible the mergeinfo changed because of a delete, and
     that the mergeinfo is now gone. If this is the case, we want
     to do nothing but fallthrough into the count == 0 case */
  if (sqlite_result == SQLITE_DONE)
    {
      *result = NULL;
      return SVN_NO_ERROR;
    }
  else if (sqlite_result == SQLITE_ROW)
    {
      apr_array_header_t *pathranges;
      const char *mergedfrom;
      svn_revnum_t startrev;
      svn_revnum_t endrev;
      svn_boolean_t inheritable;
      const char *lastmergedfrom = NULL;

      *result = apr_hash_make(pool);
      pathranges = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

      do
        {
          mergedfrom = (char *) sqlite3_column_text(stmt, 0);
          startrev = sqlite3_column_int64(stmt, 1);
          endrev = sqlite3_column_int64(stmt, 2);
          inheritable = sqlite3_column_int64(stmt, 3) == 0 ? FALSE : TRUE;

          mergedfrom = apr_pstrdup(pool, mergedfrom);
          if (lastmergedfrom && strcmp(mergedfrom, lastmergedfrom) != 0)
            {
              /* This iteration over the result set starts a group of
                 mergeinfo with a different merge source. */
              apr_hash_set(*result, lastmergedfrom, APR_HASH_KEY_STRING,
                           pathranges);
              pathranges = apr_array_make(pool, 1,
                                          sizeof(svn_merge_range_t *));
            }

          /* Filter out invalid revision numbers, which are assumed to
             represent dummy records indicating that a merge source
             has no mergeinfo for PATH. */
          if (SVN_IS_VALID_REVNUM(startrev) && SVN_IS_VALID_REVNUM(endrev))
            {
              svn_merge_range_t *range = apr_pcalloc(pool, sizeof(*range));
              range->start = startrev;
              range->end = endrev;
              range->inheritable = inheritable;
              APR_ARRAY_PUSH(pathranges, svn_merge_range_t *) = range;
            }

          sqlite_result = sqlite3_step(stmt);
          lastmergedfrom = mergedfrom;
        }
      while (sqlite_result == SQLITE_ROW);

      apr_hash_set(*result, mergedfrom, APR_HASH_KEY_STRING, pathranges);

      if (sqlite_result != SQLITE_DONE)
        return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                sqlite3_errmsg(db));
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                              sqlite3_errmsg(db));
    }
  SQLITE_ERR(sqlite3_finalize(stmt), db);

  return SVN_NO_ERROR;
}


/* Helper for get_mergeinfo_for_path() that will append
   PATH_TO_APPEND to each path that exists in the mergeinfo hash
   INPUT, and return a new mergeinfo hash in *OUTPUT.  */
static svn_error_t *
append_component_to_paths(apr_hash_t **output,
                          apr_hash_t *input,
                          const char *path_to_append,
                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  *output = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, input); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      char *newpath;

      apr_hash_this(hi, &key, NULL, &val);
      newpath = svn_path_join((const char *) key, path_to_append, pool);
      apr_hash_set(*output, newpath, APR_HASH_KEY_STRING, val);
    }

  return SVN_NO_ERROR;
}

/* A helper for svn_fs_mergeinfo__get_mergeinfo() that retrieves
   mergeinfo recursively (when INCLUDE_PARENTS is TRUE) for a single
   path.  Pass NULL for RESULT if you only want CACHE to be updated.
   Otherwise, both RESULT and CACHE are updated with the appropriate
   mergeinfo for PATH. */
static svn_error_t *
get_mergeinfo_for_path(sqlite3 *db,
                       const char *path,
                       svn_revnum_t rev,
                       apr_hash_t *result,
                       apr_hash_t *cache,
                       svn_mergeinfo_inheritance_t inherit,
                       apr_pool_t *pool)
{
  apr_hash_t *path_mergeinfo;
  sqlite3_stmt *stmt;
  int sqlite_result;
  sqlite_int64 lastmerged_rev;

  if (inherit == svn_mergeinfo_nearest_ancestor)
    {
      lastmerged_rev = 0;
    }
  else
    {
      path_mergeinfo = apr_hash_get(cache, path, APR_HASH_KEY_STRING);
      if (path_mergeinfo != NULL)
        {
          if (path_mergeinfo != NEGATIVE_CACHE_RESULT && result)
            apr_hash_set(result, path, APR_HASH_KEY_STRING, path_mergeinfo);
          return SVN_NO_ERROR;
        }

      /* See if we have a mergeinfo_changed record for this path. If not,
         then it can't have mergeinfo.  */
      SQLITE_ERR(sqlite3_prepare(db,
                                 "SELECT MAX(revision) FROM mergeinfo_changed "
                                 "WHERE path = ? AND revision <= ?;",
                                 -1, &stmt, NULL), db);

      SQLITE_ERR(sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT), db);
      SQLITE_ERR(sqlite3_bind_int64(stmt, 2, rev), db);
      sqlite_result = sqlite3_step(stmt);
      if (sqlite_result != SQLITE_ROW)
        return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                sqlite3_errmsg(db));

      lastmerged_rev = sqlite3_column_int64(stmt, 0);
      SQLITE_ERR(sqlite3_finalize(stmt), db);

      /* If we've got mergeinfo data, transform it from the db into a
         mergeinfo hash */
      if (lastmerged_rev > 0)
        {
          SVN_ERR(parse_mergeinfo_from_db(db, path, lastmerged_rev,
                                          &path_mergeinfo, pool));
          if (path_mergeinfo)
            {
              if (result)
                apr_hash_set(result, path, APR_HASH_KEY_STRING,
                             path_mergeinfo);
              apr_hash_set(cache, path, APR_HASH_KEY_STRING, path_mergeinfo);
            }
          else
            apr_hash_set(cache, path, APR_HASH_KEY_STRING,
                         NEGATIVE_CACHE_RESULT);
          return SVN_NO_ERROR;
        }
    } /* inherit != svn_mergeinfo_nearest_ancestor */

  /* If we want only this path's parent's mergeinfo or this path has no
     mergeinfo, and we are asked to, check PATH's nearest ancestor. */
  if ((lastmerged_rev == 0 && inherit == svn_mergeinfo_inherited)
      || inherit == svn_mergeinfo_nearest_ancestor)
    {
      svn_stringbuf_t *parentpath;

      /* It is possible we are already at the root.  */
      if (strcmp(path, "") == 0)
        return SVN_NO_ERROR;

      parentpath = svn_stringbuf_create(path, pool);
      svn_path_remove_component(parentpath);

      /* The repository and the mergeinfo index internally refer to
         the root path as "" rather than "/". */
      if (strcmp(parentpath->data, "/") == 0)
        parentpath->data = (char *) "";

      SVN_ERR(get_mergeinfo_for_path(db, parentpath->data, rev,
                                     NULL, cache, svn_mergeinfo_inherited,
                                     pool));
      path_mergeinfo = apr_hash_get(cache, parentpath->data,
                                    APR_HASH_KEY_STRING);
      if (path_mergeinfo == NEGATIVE_CACHE_RESULT)
        apr_hash_set(cache, path, APR_HASH_KEY_STRING, NULL);
      else if (path_mergeinfo)
        {
          /* Now translate the result for our parent to our path. */
          apr_hash_t *translated_mergeinfo;
          const char *to_append = &path[parentpath->len + 1];

          /* But first remove all non-inheritable revision ranges. */
          SVN_ERR(svn_mergeinfo_inheritable(&path_mergeinfo, path_mergeinfo,
                                            NULL, SVN_INVALID_REVNUM,
                                            SVN_INVALID_REVNUM, pool));
          append_component_to_paths(&translated_mergeinfo, path_mergeinfo,
                                    to_append, pool);
          apr_hash_set(cache, path, APR_HASH_KEY_STRING, translated_mergeinfo);
          if (result)
            apr_hash_set(result, path, APR_HASH_KEY_STRING,
                         translated_mergeinfo);
        }
    }
  return SVN_NO_ERROR;
}


/* Get the mergeinfo for all of the children of PATH in REV.  Return the
   results in PATH_MERGEINFO.  PATH_MERGEINFO should already be created
   prior to calling this function, but it's value may change as additional
   mergeinfos are added to it.  */
static svn_error_t *
get_mergeinfo_for_children(sqlite3 *db,
                           const char *path,
                           svn_revnum_t rev,
                           apr_hash_t **path_mergeinfo,
                           svn_fs_mergeinfo_filter_func_t filter_func,
                           void *filter_func_baton,
                           apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;
  char *like_path;

  /* Get all paths under us. */
  SQLITE_ERR(sqlite3_prepare(db,
                             "SELECT MAX(revision), path "
                             "FROM mergeinfo_changed "
                             "WHERE path LIKE ? AND revision <= ? "
                             "GROUP BY path;",
                             -1, &stmt, NULL), db);

  like_path = apr_psprintf(pool, "%s/%%", path);

  SQLITE_ERR(sqlite3_bind_text(stmt, 1, like_path, -1, SQLITE_TRANSIENT), db);
  SQLITE_ERR(sqlite3_bind_int64(stmt, 2, rev), db);

  sqlite_result = sqlite3_step(stmt);
  while (sqlite_result != SQLITE_DONE)
    {
      sqlite_int64 lastmerged_rev;
      const char *merged_path;

      if (sqlite_result == SQLITE_ERROR)
        return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                sqlite3_errmsg(db));

      lastmerged_rev = sqlite3_column_int64(stmt, 0);
      merged_path = (const char *) sqlite3_column_text(stmt, 1);

      /* If we've got a merged revision, go get the mergeinfo from the db */
      if (lastmerged_rev > 0)
        {
          apr_hash_t *db_mergeinfo;
          svn_boolean_t omit = FALSE;

          SVN_ERR(parse_mergeinfo_from_db(db, merged_path, lastmerged_rev,
                                          &db_mergeinfo, pool));

          if (filter_func)
            SVN_ERR(filter_func(filter_func_baton, &omit, merged_path,
                                db_mergeinfo, pool));

          if (!omit)
            SVN_ERR(svn_mergeinfo_merge(path_mergeinfo, db_mergeinfo,
                                        svn_rangelist_equal_inheritance,
                                        pool));
        }

      sqlite_result = sqlite3_step(stmt);
    }

  SQLITE_ERR(sqlite3_finalize(stmt), db);

  return SVN_NO_ERROR;
}

/* Get the mergeinfo for a set of paths, returned as a hash of mergeinfo
   hashs keyed by each path. */
static svn_error_t *
get_mergeinfo(sqlite3 *db,
              apr_hash_t **mergeinfo,
              svn_fs_root_t *root,
              const apr_array_header_t *paths,
              svn_mergeinfo_inheritance_t inherit,
              apr_pool_t *pool)
{
  apr_hash_t *mergeinfo_cache = apr_hash_make(pool);
  svn_revnum_t rev;
  int i;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  rev = REV_ROOT_REV(root);

  *mergeinfo = apr_hash_make(pool);
  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      SVN_ERR(get_mergeinfo_for_path(db, path, rev, *mergeinfo,
                                     mergeinfo_cache, inherit, pool));
    }

  return SVN_NO_ERROR;
}

/* Get the mergeinfo for a set of paths.  */
svn_error_t *
svn_fs_mergeinfo__get_mergeinfo(apr_hash_t **mergeinfo,
                                svn_fs_root_t *root,
                                const apr_array_header_t *paths,
                                svn_mergeinfo_inheritance_t inherit,
                                apr_pool_t *pool)
{
  sqlite3 *db;
  int i;
  svn_error_t *err;

  SVN_ERR(open_db(&db, root->fs->path, pool));
  err = get_mergeinfo(db, mergeinfo, root, paths, inherit, pool);
  SVN_ERR(close_db(db, err));

  for (i = 0; i < paths->nelts; i++)
    {
      svn_stringbuf_t *mergeinfo_buf;
      apr_hash_t *path_mergeinfo;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      path_mergeinfo = apr_hash_get(*mergeinfo, path, APR_HASH_KEY_STRING);
      if (path_mergeinfo)
        {
          SVN_ERR(svn_mergeinfo_sort(path_mergeinfo, pool));
          SVN_ERR(svn_mergeinfo_to_stringbuf(&mergeinfo_buf, path_mergeinfo,
                                             pool));
          apr_hash_set(*mergeinfo, path, APR_HASH_KEY_STRING,
                       mergeinfo_buf->data);
        }
    }


  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_mergeinfo__get_mergeinfo_for_tree(apr_hash_t **mergeinfo,
                                         svn_fs_root_t *root,
                                         const apr_array_header_t *paths,
                                         svn_fs_mergeinfo_filter_func_t filter_func,
                                         void *filter_func_baton,
                                         apr_pool_t *pool)
{
  svn_error_t *err;
  svn_revnum_t rev;
  sqlite3 *db;
  int i;

  SVN_ERR(open_db(&db, root->fs->path, pool));
  err = get_mergeinfo(db, mergeinfo, root, paths, svn_mergeinfo_inherited,
                      pool);
  MAYBE_CLEANUP;

  rev = REV_ROOT_REV(root);

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      apr_hash_t *path_mergeinfo = apr_hash_get(*mergeinfo, path,
                                                APR_HASH_KEY_STRING);

      if (!path_mergeinfo)
        path_mergeinfo = apr_hash_make(pool);

      if (filter_func)
        {
          svn_boolean_t omit;

          err = filter_func(filter_func_baton, &omit, path, path_mergeinfo,
                            pool);
          MAYBE_CLEANUP;

          if (omit)
            {
              apr_hash_set(*mergeinfo, path, APR_HASH_KEY_STRING, NULL);
              continue;
            }
        }

      err = get_mergeinfo_for_children(db, path, rev, &path_mergeinfo,
                                       filter_func, filter_func_baton, pool);
      MAYBE_CLEANUP;

      apr_hash_set(*mergeinfo, path, APR_HASH_KEY_STRING, path_mergeinfo);
    }

 cleanup:
  return close_db(db, err);
}
