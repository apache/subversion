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

#include <apr.h>
#include <apr_general.h>
#include <apr_pools.h>

#include <sqlite3.h>

#include "svn_fs.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "private/svn_dep_compat.h"
#include "private/svn_fs_sqlite.h"
#include "private/svn_fs_mergeinfo.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"

/* This is a macro implementation of svn_fs_revision_root_revision(), which
   we cannot call from here, because it would create a circular dependency. */
#define REV_ROOT_REV(root)       \
  ((root)->is_txn_root ? SVN_INVALID_REVNUM : (root)->rev)

/* We want to cache that we saw no mergeinfo for a path as well,
   so we use a -1 converted to a pointer to represent this. */
#define NEGATIVE_CACHE_RESULT ((void *)(-1))

/* A flow-control helper macro for sending processing to the 'cleanup'
  label when the local variable 'err' is not SVN_NO_ERROR. */
#define MAYBE_CLEANUP if (err) goto cleanup


static svn_error_t *
get_mergeinfo_for_path(sqlite3 *db,
                       const char *path,
                       svn_revnum_t rev,
                       apr_hash_t *result,
                       apr_hash_t *cache,
                       svn_mergeinfo_inheritance_t inherit,
                       apr_pool_t *pool);

static svn_error_t *
get_mergeinfo(sqlite3 *db,
              apr_hash_t **mergeinfo,
              svn_revnum_t rev,
              const apr_array_header_t *paths,
              svn_mergeinfo_inheritance_t inherit,
              apr_pool_t *pool);

/* Represents "no mergeinfo". */
static svn_merge_range_t no_mergeinfo = { SVN_INVALID_REVNUM,
                                          SVN_INVALID_REVNUM };

/* Insert the necessary indexing data into the DB for all the merges
   on PATH as of NEW_REV, which is provided in CURR_MERGEINFO.
   ORIG_MERGEINFO corresponds to pre-commit mergeinfo.
   ADDED_MERGEINFO corresponds to fresh merges in this commit.
   'mergeinfo' table is populated with CURR_MERGEINFO.
   'mergeinfo_table' table is populated with ADDED_MERGEINFO.
   Use POOL for temporary allocations.*/

static svn_error_t *
index_path_mergeinfo(svn_revnum_t new_rev,
                     sqlite3 *db,
                     const char *path,
                     apr_hash_t *curr_mergeinfo,
                     apr_hash_t *orig_mergeinfo,
                     apr_hash_t *added_mergeinfo,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  sqlite3_stmt *stmt;
  svn_boolean_t remove_mergeinfo = FALSE;

  if (apr_hash_count(curr_mergeinfo) == 0)
    {
      if (orig_mergeinfo == NULL)
        /* There was previously no mergeinfo, inherited or explicit,
           for PATH. */
        return SVN_NO_ERROR;

      /* All mergeinfo has been removed from PATH (or explicitly set
         to "none", if there previously was no mergeinfo).  Find all
         previous mergeinfo, and (further below) insert dummy records
         representing "no mergeinfo" for all its previous merge
         sources of PATH. */
      remove_mergeinfo = TRUE;
      curr_mergeinfo = orig_mergeinfo;
    }

  for (hi = apr_hash_first(NULL, curr_mergeinfo);
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
          SVN_FS__SQLITE_ERR(sqlite3_prepare
                             (db,
                              "INSERT INTO mergeinfo (revision, mergedfrom, "
                              "mergedto, mergedrevstart, mergedrevend, "
                              "inheritable) VALUES (?, ?, ?, ?, ?, ?);",
                              -1, &stmt, NULL), db);
          SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 1, new_rev), db);
          SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 2, from, -1, 
                                               SQLITE_TRANSIENT),
                             db);
          SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 3, path, -1, 
                                               SQLITE_TRANSIENT),
                             db);

          if (remove_mergeinfo)
            {
              /* Explicitly set "no mergeinfo" for PATH, which may've
                 previously had only inherited mergeinfo. */
#if APR_VERSION_AT_LEAST(1, 3, 0)
              apr_array_clear(rangelist);
#else
              /* Use of an iterpool would be overkill here. */
              rangelist = apr_array_make(pool, 1, sizeof(&no_mergeinfo));
#endif
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = &no_mergeinfo;
            }

          for (i = 0; i < rangelist->nelts; i++)
            {
              const svn_merge_range_t *range =
                APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 4, range->start),
                                 db);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 5, range->end),
                                 db);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 6, 
                                                    range->inheritable), db);
              SVN_ERR(svn_fs__sqlite_step_done(stmt));

              SVN_FS__SQLITE_ERR(sqlite3_reset(stmt), db);
            }
          SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
        }
    }

  for (hi = apr_hash_first(NULL, added_mergeinfo);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *mergedfrom;
      apr_array_header_t *rangelist;
      const void *key;
      void *val;
      apr_hash_this(hi, &key, NULL, &val);
      mergedfrom = key;
      rangelist = val;
      if (mergedfrom && rangelist)
        {
          int i;
          SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                                    "INSERT INTO mergeinfo_changed (revision, "
                                    "mergedfrom, mergedto, mergedrevstart, "
                                    "mergedrevend, inheritable) "
                                    "VALUES (?, ?, ?, ?, ?, ?);", -1, &stmt,
                                    NULL), db);

          SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 1, new_rev), db);
          SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 2, mergedfrom, -1, 
                                               SQLITE_TRANSIENT),
                             db);
          SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 3, path, -1,
                                               SQLITE_TRANSIENT), 
                             db);
          for (i = 0; i < rangelist->nelts; i++)
            {
              const svn_merge_range_t *range =
                          APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 4, range->start),
                                 db);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 5, range->end), db);
              SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 6, 
                                                    range->inheritable), 
                                 db);
              SVN_ERR(svn_fs__sqlite_step_done(stmt));
              SVN_FS__SQLITE_ERR(sqlite3_reset(stmt), db);
            }
          SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
        }
    }
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
  apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
  apr_hash_t *orig_mergeinfo_for_paths;
  for (hi = apr_hash_first(pool, mergeinfo_for_paths);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *path;
      apr_hash_this(hi, &path, NULL, NULL);
      APR_ARRAY_PUSH(paths, const char *) = path;
    }

  SVN_ERR(get_mergeinfo(db, &orig_mergeinfo_for_paths, new_rev-1, paths,
                        svn_mergeinfo_inherited, pool));

  for (hi = apr_hash_first(pool, mergeinfo_for_paths);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *path;
      void *mergeinfo;
      apr_hash_t *curr_mergeinfo;
      apr_hash_t *orig_mergeinfo_for_path;
      apr_hash_t *added_mergeinfo_for_path;
      apr_hash_t *deleted_mergeinfo_for_path;

      apr_hash_this(hi, &path, NULL, &mergeinfo);
      orig_mergeinfo_for_path = apr_hash_get(orig_mergeinfo_for_paths, path,
                                             APR_HASH_KEY_STRING);
      SVN_ERR(svn_mergeinfo_parse(&curr_mergeinfo,
                                  ((svn_string_t *)mergeinfo)->data,pool));
      SVN_ERR(svn_mergeinfo_diff(&deleted_mergeinfo_for_path,
                                 &added_mergeinfo_for_path,
                                 orig_mergeinfo_for_path, curr_mergeinfo, TRUE,
                                 pool));
      SVN_ERR(index_path_mergeinfo(new_rev, db, (const char *) path,
                                   curr_mergeinfo, orig_mergeinfo_for_path,
                                   added_mergeinfo_for_path, pool));

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
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs__sqlite_open(&db, txn->fs->path, subpool));
  err = svn_fs__sqlite_exec(db, "BEGIN TRANSACTION;");
  MAYBE_CLEANUP;

  /* Cleanup the leftovers of any previous, failed transactions
   * involving NEW_REV. */
  deletestring = apr_psprintf(subpool,
                              "DELETE FROM mergeinfo_changed WHERE "
                              "revision = %ld;",
                              new_rev);
  err = svn_fs__sqlite_exec(db, deletestring);
  MAYBE_CLEANUP;
  deletestring = apr_psprintf(subpool,
                              "DELETE FROM mergeinfo WHERE revision = %ld;",
                              new_rev);
  err = svn_fs__sqlite_exec(db, deletestring);
  MAYBE_CLEANUP;

  /* Record any mergeinfo from the current transaction. */
  if (mergeinfo_for_paths)
    {
      err = index_txn_mergeinfo(db, new_rev, mergeinfo_for_paths, subpool);
      MAYBE_CLEANUP;
    }

  /* This is moved here from FSFS's commit_txn, because we don't want to
   * write the final current file if the sqlite commit fails.
   * On the other hand, if we commit the transaction and end up failing
   * the current file, we just end up with inaccessible data in the
   * database, not a real problem.  */
  err = svn_fs__sqlite_exec(db, "COMMIT TRANSACTION;");
  MAYBE_CLEANUP;

 cleanup:
  err = svn_fs__sqlite_close(db, err);
  svn_pool_destroy(subpool);
  return err;
}

/* Helper for get_mergeinfo_for_path() that retrieves mergeinfo for
   PATH at the revision LASTMERGED_REV, returning it in the merge info
   hash *RESULT (with rangelist elements in ascending order).  Perform
   all allocations in POOL. */
static svn_error_t *
parse_mergeinfo_from_db(sqlite3 *db,
                        const char *path,
                        svn_revnum_t lastmerged_rev,
                        apr_hash_t **result,
                        apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;

  SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                                     "SELECT mergedfrom, mergedrevstart, "
                                     "mergedrevend, inheritable FROM mergeinfo "
                                     "WHERE mergedto = ? AND revision = ? "
                                     "ORDER BY mergedfrom, mergedrevstart;",
                                     -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT), 
                     db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 2, lastmerged_rev), db);
  sqlite_result = sqlite3_step(stmt);

  /* It is possible the mergeinfo changed because of a delete, and
     that the mergeinfo is now gone. If this is the case, we want
     to do nothing but fallthrough into the count == 0 case */
  if (sqlite_result == SQLITE_DONE)
    {
      *result = NULL;
      SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
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
          startrev = (svn_revnum_t) sqlite3_column_int64(stmt, 1);
          endrev = (svn_revnum_t) sqlite3_column_int64(stmt, 2);
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
        return svn_fs__sqlite_stmt_error(stmt);
      
      SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
      return SVN_NO_ERROR;
    }
  else
    return svn_fs__sqlite_stmt_error(stmt);
}


/* Helper for get_mergeinfo_for_path() that will append PATH_TO_APPEND
   to each path that exists in the mergeinfo hash INPUT, and return a
   new mergeinfo hash in *OUTPUT.  Perform all allocations in POOL. */
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
      newpath = svn_path_join((const char *) key, path_to_append,
                              apr_hash_pool_get(*output));
      apr_hash_set(*output, newpath, APR_HASH_KEY_STRING, val);
    }

  return SVN_NO_ERROR;
}

/* A helper for svn_fs_mergeinfo__get_mergeinfo() that retrieves
   mergeinfo on PATH at REV from DB by taking care of elided mergeinfo
   if INHERIT is svn_mergeinfo_inherited or svn_mergeinfo_nearest_ancestor.
   Pass NULL for RESULT if you only want CACHE to be 
   updated.  Otherwise, both RESULT and CACHE are updated with the appropriate
   mergeinfo for PATH.

   Perform all allocation in POOL.  Due to the nature of APR pools,
   and the recursion in this function, invoke this function using a
   sub-pool.  To preserve RESULT, use mergeinfo_hash_dup() before
   clearing or destroying POOL. */
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
  svn_revnum_t lastmerged_rev;

  if (inherit == svn_mergeinfo_nearest_ancestor)
    {
      /* Looking for (possibly inherited) mergeinfo from PATH ancestors. */
      lastmerged_rev = 0;
    }
  else
    {
      /* Lookup the mergeinfo for PATH, starting with the cache, the
         moving on to the SQLite index.. */
      path_mergeinfo = apr_hash_get(cache, path, APR_HASH_KEY_STRING);
      if (path_mergeinfo)
        {
          /* We already had a mergeinfo lookup attempt cached. */
          if (result && path_mergeinfo != NEGATIVE_CACHE_RESULT)
            apr_hash_set(result, path, APR_HASH_KEY_STRING, path_mergeinfo);
          return SVN_NO_ERROR;
        }

      /* See if we have a mergeinfo_changed record for this path. If not,
         then it can't have mergeinfo.  */
      SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                                         "SELECT MAX(revision) FROM "
                                         "mergeinfo_changed WHERE "
                                         "mergedto = ? AND revision <= ?;",
                                         -1, &stmt, NULL), db);

      SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT),
                         db);
      SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 2, rev), db);
      sqlite_result = sqlite3_step(stmt);
      if (sqlite_result != SQLITE_ROW)
        return svn_fs__sqlite_stmt_error(stmt);

      lastmerged_rev = (svn_revnum_t) sqlite3_column_int64(stmt, 0);
      SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

      /* If we've got mergeinfo data, transform it from the db into a
         mergeinfo hash.  Either way, cache whether we found mergeinfo. */
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
        svn_stringbuf_set(parentpath, "");

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


/* Get the mergeinfo for all of the children of PATH in REV.  Return
   the results in PATH_MERGEINFO.  PATH_MERGEINFO should already be
   created prior to calling this function, but it's value may change
   as additional mergeinfos are added to it.  Returned values are
   allocated in POOL, while temporary values are allocated in a
   sub-pool. */
static svn_error_t *
get_mergeinfo_for_children(sqlite3 *db,
                           const char *path,
                           svn_revnum_t rev,
                           apr_hash_t *path_mergeinfo,
                           svn_fs_mergeinfo_filter_func_t filter_func,
                           void *filter_func_baton,
                           apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;
  apr_pool_t *subpool = svn_pool_create(pool);
  char *like_path;

  /* Get all paths under us. */
  SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                                     "SELECT MAX(revision), mergedto "
                                     "FROM mergeinfo_changed "
                                     "WHERE mergedto LIKE ? "
                                     "AND revision <= ? "
                                     "GROUP BY mergedto;",
                                     -1, &stmt, NULL), db);

  like_path = apr_psprintf(subpool, "%s/%%", path);

  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, like_path, -1, 
                                       SQLITE_TRANSIENT), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 2, rev), db);

  sqlite_result = sqlite3_step(stmt);
  while (sqlite_result == SQLITE_ROW)
    {
      svn_revnum_t lastmerged_rev;
      const char *merged_path;

      svn_pool_clear(subpool);

      lastmerged_rev = (svn_revnum_t) sqlite3_column_int64(stmt, 0);
      merged_path = (const char *) sqlite3_column_text(stmt, 1);

      /* If we've got a merged revision, go get the mergeinfo from the db */
      if (lastmerged_rev > 0)
        {
          apr_hash_t *db_mergeinfo;
          svn_boolean_t omit = FALSE;

          SVN_ERR(parse_mergeinfo_from_db(db, merged_path, lastmerged_rev,
                                          &db_mergeinfo, subpool));

          if (filter_func)
            SVN_ERR(filter_func(filter_func_baton, &omit, merged_path,
                                db_mergeinfo, subpool));

          if (!omit)
            SVN_ERR(svn_mergeinfo_merge(path_mergeinfo, db_mergeinfo, pool));
        }

      sqlite_result = sqlite3_step(stmt);
    }

  if (sqlite_result != SQLITE_DONE)
    return svn_fs__sqlite_stmt_error(stmt);

  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Return a deep copy of MERGEINFO_HASH (allocated in POOL), which is
   a hash of paths -> mergeinfo hashes. */
static apr_hash_t *
mergeinfo_hash_dup(apr_hash_t *mergeinfo_hash, apr_pool_t *pool)
{
  apr_hash_t *new_hash = apr_hash_make(pool);
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(NULL, mergeinfo_hash); hi; hi = apr_hash_next(hi))
    {
      const void *path;
      apr_ssize_t klen;
      void *mergeinfo;

      apr_hash_this(hi, &path, &klen, &mergeinfo);
      apr_hash_set(new_hash, path, klen,
                   svn_mergeinfo_dup((apr_hash_t *) mergeinfo,
                                     apr_hash_pool_get(new_hash)));
    }
  return new_hash;
}

/* Get the mergeinfo for a set of paths, returned in *MERGEINFO_HASH
   as a hash of mergeinfo hashes keyed by each path.  Returned values
   are allocated in POOL, while temporary values are allocated in a
   sub-pool. */
static svn_error_t *
get_mergeinfo(sqlite3 *db,
              apr_hash_t **mergeinfo_hash,
              svn_revnum_t rev,
              const apr_array_header_t *paths,
              svn_mergeinfo_inheritance_t inherit,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *result_hash = apr_hash_make(subpool);
  apr_hash_t *cache_hash = apr_hash_make(subpool);
  int i;

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      SVN_ERR(get_mergeinfo_for_path(db, path, rev, result_hash, cache_hash,
                                     inherit, apr_hash_pool_get(result_hash)));
    }

  *mergeinfo_hash = mergeinfo_hash_dup(result_hash, pool);
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Get the mergeinfo for a set of paths.  Returned values are
   allocated in POOL, while temporary values are allocated in a
   sub-pool. */
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
  svn_revnum_t rev;
  apr_pool_t *subpool;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);
  rev = REV_ROOT_REV(root);

  subpool = svn_pool_create(pool);

  /* Retrieve a path -> mergeinfo hash mapping. */
  SVN_ERR(svn_fs__sqlite_open(&db, root->fs->path, subpool));
  err = get_mergeinfo(db, mergeinfo, rev, paths, inherit, pool);
  SVN_ERR(svn_fs__sqlite_close(db, err));

  /* Convert each mergeinfo hash value into a textual representation. */
  for (i = 0; i < paths->nelts; i++)
    {
      svn_stringbuf_t *mergeinfo_buf;
      apr_hash_t *path_mergeinfo;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);

      path_mergeinfo = apr_hash_get(*mergeinfo, path, APR_HASH_KEY_STRING);
      if (path_mergeinfo)
        {
          SVN_ERR(svn_mergeinfo_sort(path_mergeinfo, subpool));
          SVN_ERR(svn_mergeinfo_to_stringbuf(&mergeinfo_buf, path_mergeinfo,
                                             apr_hash_pool_get(*mergeinfo)));
          apr_hash_set(*mergeinfo, path, APR_HASH_KEY_STRING,
                       mergeinfo_buf->data);
        }
    }
  svn_pool_destroy(subpool);

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

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);
  rev = REV_ROOT_REV(root);

  SVN_ERR(svn_fs__sqlite_open(&db, root->fs->path, pool));
  err = get_mergeinfo(db, mergeinfo, rev, paths, svn_mergeinfo_inherited,
                      pool);
  MAYBE_CLEANUP;

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

      err = get_mergeinfo_for_children(db, path, rev, path_mergeinfo,
                                       filter_func, filter_func_baton, pool);
      MAYBE_CLEANUP;

      apr_hash_set(*mergeinfo, path, APR_HASH_KEY_STRING, path_mergeinfo);
    }

 cleanup:
  return svn_fs__sqlite_close(db, err);
}

/* Helper function for 'get_commit_revs_for_merge_ranges', to identify the
   correct PARENT_WITH_MERGEINFO where the mergeinfo of 'MERGE_TARGET' 
   has been elided to. DB is used to retrieve this data within the 
   commits 'min_commit_rev(exclusive):max_commit_rev(inclusive)'.
*/
static svn_error_t *
get_parent_target_path_having_mergeinfo(const char** parent_with_mergeinfo,
                                        sqlite3 *db,
                                        const char* merge_target,
                                        svn_revnum_t min_commit_rev,
                                        svn_revnum_t max_commit_rev,
                                        apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;
  svn_boolean_t has_mergeinfo = FALSE;
  SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                             "SELECT revision FROM mergeinfo_changed WHERE"
                             " mergedto = ? AND revision between ? AND ?;",
                             -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, merge_target, 
                                       -1, SQLITE_TRANSIENT),
                     db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 2, min_commit_rev+1), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 3, max_commit_rev), db);
  sqlite_result = sqlite3_step(stmt);
  SVN_FS__SQLITE_ERR(sqlite3_reset(stmt), db);
  if (sqlite_result == SQLITE_ROW)
    {
      *parent_with_mergeinfo = apr_pstrdup(pool, merge_target);
      has_mergeinfo = TRUE;
    }
  else
    {
      const char *parent_path = svn_path_dirname(merge_target, pool);
      while (strcmp(parent_path, "/"))
        {
          SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, parent_path, -1,
                             SQLITE_TRANSIENT), db);
          sqlite_result = sqlite3_step(stmt);
          SVN_FS__SQLITE_ERR(sqlite3_reset(stmt), db);
          if (sqlite_result == SQLITE_ROW)
            {
              *parent_with_mergeinfo = parent_path;
              has_mergeinfo = TRUE;
              break;
            }
          else
            parent_path = svn_path_dirname(parent_path, pool);
        }
    }

  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

  if (has_mergeinfo)
    return SVN_NO_ERROR;
  else
    return svn_error_createf(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            _("No mergeinfo for %s between revisions %ld:%ld"),
                               merge_target, min_commit_rev, max_commit_rev);
}

/* Helper function for 'svn_fs_mergeinfo__get_commit_revs_for_merge_ranges'.
   Retrieves the commit revisions for each merge range from MERGE_RANGELIST,
   for the merge from MERGE_SOURCE to MERGE_TARGET within the time 
   > MIN_COMMIT_REV and <=MAX_COMMIT_REV.
   INHERIT decides whether to get the commit rev from parent paths or not.
   It uses DB for retrieving this data. Commit revisions identified are 
   populated in *COMMIT_REV_RANGELIST as each in its own 
   single rev *svn_merge_range_t.
*/
static svn_error_t *
get_commit_revs_for_merge_ranges(apr_array_header_t **commit_rev_rangelist,
                                 sqlite3 *db,
                                 const char* merge_target,
                                 const char* merge_source,
                                 svn_revnum_t min_commit_rev,
                                 svn_revnum_t max_commit_rev,
                                 const apr_array_header_t *merge_rangelist,
                                 svn_mergeinfo_inheritance_t inherit,
                                 apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;
  int i;
  const char *parent_with_mergeinfo = merge_target;
  const char *parent_merge_source = merge_source;

  /* early return */
  if (!merge_rangelist || !merge_rangelist->nelts)
    return SVN_NO_ERROR;
  if (inherit == svn_mergeinfo_inherited
      || inherit == svn_mergeinfo_nearest_ancestor)
    SVN_ERR(get_parent_target_path_having_mergeinfo(&parent_with_mergeinfo,
                                                    db, merge_target,
                                                    min_commit_rev,
                                                    max_commit_rev, pool));
  if (strcmp(parent_with_mergeinfo, merge_target))
    {
      int parent_merge_src_end;
      const char *target_base_name = merge_target
                                               + strlen(parent_with_mergeinfo);
      parent_merge_src_end = strlen(merge_source) - strlen(target_base_name);
      parent_merge_source = apr_pstrndup(pool, merge_source,
                                         parent_merge_src_end);
    }
  *commit_rev_rangelist = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
  SVN_FS__SQLITE_ERR(sqlite3_prepare(db,
                             "SELECT MAX(revision) FROM mergeinfo_changed "
                             "WHERE mergedfrom = ? AND mergedto = ? "
                             "AND mergedrevstart = ? AND mergedrevend = ? "
                             "AND inheritable = ? AND revision <= ?;",
                             -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, parent_merge_source, -1,
                                       SQLITE_TRANSIENT), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 2, parent_with_mergeinfo, -1,
                                       SQLITE_TRANSIENT), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 6, max_commit_rev), db);
  for (i = 0; i < merge_rangelist->nelts; i++)
    {
      const svn_merge_range_t *range = APR_ARRAY_IDX(merge_rangelist, i,
                                                     svn_merge_range_t *);
      svn_merge_range_t *commit_rev_range;
      svn_revnum_t commit_rev;
      SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 3, range->start), db);
      SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 4, range->end), db);
      SVN_FS__SQLITE_ERR(sqlite3_bind_int64(stmt, 5, range->inheritable), db);
      sqlite_result = sqlite3_step(stmt);
      if (sqlite_result != SQLITE_ROW)
        return svn_error_createf(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                 _("No commit rev for merge %ld:%ld from %s"
                                   " on %s within %ld"), range->start,
                                 range->end,
                                 merge_source, merge_target, max_commit_rev);
      commit_rev_range = apr_pcalloc(pool, sizeof(*commit_rev_range));
      commit_rev = (svn_revnum_t) sqlite3_column_int64(stmt, 0);
      commit_rev_range->start = commit_rev - 1;
      commit_rev_range->end = commit_rev;
      commit_rev_range->inheritable = TRUE;
      APR_ARRAY_PUSH(*commit_rev_rangelist,
                     svn_merge_range_t *) = commit_rev_range;
      SVN_FS__SQLITE_ERR(sqlite3_reset(stmt), db);
    }
  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);
  qsort((*commit_rev_rangelist)->elts, (*commit_rev_rangelist)->nelts, 
        (*commit_rev_rangelist)->elt_size, svn_sort_compare_ranges);
  return SVN_NO_ERROR;
}

/* Retrieves the commit revisions for each merge range from MERGE_RANGELIST,
   for the merge from MERGE_SOURCE to MERGE_TARGET within the time 
   > MIN_COMMIT_REV and <=MAX_COMMIT_REV.
   INHERIT decides whether to get the commit rev from parent paths or not.
   Commit revisions identified are populated in *COMMIT_REV_RANGELIST as each
   in its own single rev *svn_merge_range_t.
*/
svn_error_t *
svn_fs_mergeinfo__get_commit_revs_for_merge_ranges(
                                     apr_array_header_t **commit_rev_rangelist,
                                     svn_fs_root_t *root,
                                     const char* merge_target,
                                     const char* merge_source,
                                     svn_revnum_t min_commit_rev,
                                     svn_revnum_t max_commit_rev,
                                     const apr_array_header_t *merge_rangelist,
                                     svn_mergeinfo_inheritance_t inherit,
                                     apr_pool_t *pool)
{
  sqlite3 *db;
  svn_error_t *err;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  SVN_ERR(svn_fs__sqlite_open(&db, root->fs->path, pool));
  err = get_commit_revs_for_merge_ranges(commit_rev_rangelist, db,
                                         merge_target, merge_source,
                                         min_commit_rev, max_commit_rev,
                                         merge_rangelist, inherit, pool);
  SVN_ERR(svn_fs__sqlite_close(db, err));
  return SVN_NO_ERROR;
}
