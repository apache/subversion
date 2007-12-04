/* mergeinfo-sqlite-index.c
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

#include "private/svn_dep_compat.h"
#include "private/svn_fs_sqlite.h"
#include "private/svn_fs_mergeinfo.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"

/*
 * A general warning about the mergeinfo tables:
 *
 * The sqlite transaction is committed (immediately) before the actual
 * FS transaction is committed.  Thus, any query against any mergeinfo
 * table MUST contain a guard on the revision column guaranteeing that
 * the returned rows have a revision value no greater than some
 * known-committed revision number!
 */


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
get_mergeinfo_for_path(svn_fs_root_t *root,
                       const char *path,
                       apr_hash_t *result,
                       apr_hash_t *cache,
                       svn_mergeinfo_inheritance_t inherit,
                       apr_pool_t *pool);

/* Represents "no mergeinfo". */
static svn_merge_range_t no_mergeinfo = { SVN_INVALID_REVNUM,
                                          SVN_INVALID_REVNUM,
                                          TRUE };

/* Insert the necessary indexing data into the DB for all the merges
   on PATH as of NEW_REV, which is provided (unparsed) in
   MERGEINFO_STR.  OLD_ROOT should be a revision root for rev NEW_REV-1.  
   Use POOL for temporary allocations.*/
static svn_error_t *
index_path_mergeinfo(svn_revnum_t new_rev,
                     sqlite3 *db,
                     const char *path,
                     svn_string_t *mergeinfo_str,
                     svn_fs_root_t *old_root,
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
         sources of PATH.

         Even though POOL is for temporary allocations, invocation of
         get_mergeinfo_for_path() necessitates its own sub-pool. */
      apr_pool_t *subpool = svn_pool_create(pool);
      apr_hash_t *cache = apr_hash_make(subpool);

      remove_mergeinfo = TRUE;
      SVN_ERR(get_mergeinfo_for_path(old_root, path, NULL, cache,
                                     svn_mergeinfo_inherited, subpool));

      mergeinfo = apr_hash_get(cache, path, APR_HASH_KEY_STRING);
      if (mergeinfo == NEGATIVE_CACHE_RESULT)
        mergeinfo = NULL;
      if (mergeinfo)
        mergeinfo = svn_mergeinfo_dup(mergeinfo, pool);

      svn_pool_destroy(subpool);

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
          SVN_ERR(svn_fs__sqlite_prepare(&stmt, db,
                                         "INSERT INTO mergeinfo (revision, "
                                         "mergedfrom, mergedto, mergedrevstart, "
                                         "mergedrevend, inheritable) VALUES (?, "
                                         "?, ?, ?, ?, ?);"));
          SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 1, new_rev));
          SVN_ERR(svn_fs__sqlite_bind_text(stmt, 2, from));
          SVN_ERR(svn_fs__sqlite_bind_text(stmt, 3, path));

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
              SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 4, range->start));
              SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 5, range->end));
              SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 6, range->inheritable));
              SVN_ERR(svn_fs__sqlite_step_done(stmt));

              SVN_ERR(svn_fs__sqlite_reset(stmt));
            }
          SVN_ERR(svn_fs__sqlite_finalize(stmt));
        }
    }

  SVN_ERR(svn_fs__sqlite_prepare(&stmt, db,
                                 "INSERT INTO mergeinfo_changed (revision, "
                                 "path) VALUES (?, ?);"));
  SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 1, new_rev));
  SVN_ERR(svn_fs__sqlite_bind_text(stmt, 2, path));

  SVN_ERR(svn_fs__sqlite_step_done(stmt));

  SVN_ERR(svn_fs__sqlite_finalize(stmt));

  return SVN_NO_ERROR;
}


/* Index the mergeinfo for each path in MERGEINFO_FOR_PATHS (a
   mapping of const char * -> to svn_string_t *). */
static svn_error_t *
index_txn_mergeinfo(sqlite3 *db,
                    svn_revnum_t new_rev,
                    apr_hash_t *mergeinfo_for_paths,
                    svn_fs_t *fs,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_fs_root_t *old_root;

  SVN_ERR(svn_fs_revision_root(&old_root, fs, new_rev - 1, pool));

  for (hi = apr_hash_first(pool, mergeinfo_for_paths);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *path;
      void *mergeinfo;

      apr_hash_this(hi, &path, NULL, &mergeinfo);
      SVN_ERR(index_path_mergeinfo(new_rev, db, (const char *) path,
                                   (svn_string_t *) mergeinfo, old_root, pool));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
table_has_any_rows_with_rev(svn_boolean_t *has_any,
                            sqlite3 *db,
                            const char *table,
                            svn_revnum_t rev,
                            apr_pool_t *pool)
{
  /* Note that we can't use the bind API for table names.  (And if
     we're sprintfing once, we might as well plug in the revision
     while we're at it; it's safe.) */
  const char *selection = apr_psprintf(pool,
                                       "SELECT 1 from %s WHERE "
                                       "revision = %ld;",
                                       table, rev);
  sqlite3_stmt *stmt;

  SVN_ERR(svn_fs__sqlite_prepare(&stmt, db, selection));
  SVN_ERR(svn_fs__sqlite_step(has_any, stmt));
  SVN_ERR(svn_fs__sqlite_finalize(stmt));
  
  return SVN_NO_ERROR;
}

/* Remove any mergeinfo already stored at NEW_REV from DB.  (This will
   exist if a previous transaction failed between sqlite
   commit-transaction and svn commit-transaction time, say.)  If
   AVOID_NOOP_DELETE is true, only run the delete commands if there's
   definitely data there to delete.
 */
static svn_error_t *
clean_tables(sqlite3 *db, 
             svn_revnum_t new_rev,
             svn_boolean_t avoid_noop_delete,
             apr_pool_t *pool)
{
  const char *deletestring;

  if (avoid_noop_delete)
    {
      svn_boolean_t has_any;
      SVN_ERR(table_has_any_rows_with_rev(&has_any, db, "mergeinfo",
                                          new_rev, pool));

      if (! has_any)
        SVN_ERR(table_has_any_rows_with_rev(&has_any, db, "mergeinfo_changed",
                                            new_rev, pool));

      if (! has_any)
        return SVN_NO_ERROR;
    }

  deletestring = apr_psprintf(pool,
                              "DELETE FROM mergeinfo_changed WHERE "
                              "revision = %ld;",
                              new_rev);
  SVN_ERR(svn_fs__sqlite_exec(db, deletestring));

  deletestring = apr_psprintf(pool,
                              "DELETE FROM mergeinfo WHERE revision = %ld;",
                              new_rev);
  SVN_ERR(svn_fs__sqlite_exec(db, deletestring));
  
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
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs__sqlite_open(&db, txn->fs->path, subpool));
  err = svn_fs__sqlite_exec(db, "BEGIN TRANSACTION;");
  MAYBE_CLEANUP;

  /* Clean up old data.  (If we're going to write to the DB anyway,
     there's no reason to do extra checks to avoid no-op DELETEs.) */
  err = clean_tables(db, 
                     new_rev, 
                     (mergeinfo_for_paths == NULL),
                     subpool);
  MAYBE_CLEANUP;

  /* Record any mergeinfo from the current transaction. */
  if (mergeinfo_for_paths)
    {
      err = index_txn_mergeinfo(db, new_rev, mergeinfo_for_paths, txn->fs, 
                                subpool);
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
   PATH at the revision LASTMERGED_REV, returning it in the mergeinfo
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
  svn_boolean_t got_row;

  SVN_ERR(svn_fs__sqlite_prepare(&stmt, db,
                                 "SELECT mergedfrom, mergedrevstart, "
                                 "mergedrevend, inheritable FROM mergeinfo "
                                 "WHERE mergedto = ? AND revision = ? "
                                 "ORDER BY mergedfrom, mergedrevstart;"));
  SVN_ERR(svn_fs__sqlite_bind_text(stmt, 1, path));
  SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 2, lastmerged_rev));
  SVN_ERR(svn_fs__sqlite_step(&got_row, stmt));

  /* It is possible the mergeinfo changed because of a delete, and
     that the mergeinfo is now gone. */
  if (! got_row)
    *result = NULL;
  else
    {
      apr_array_header_t *pathranges;
      const char *mergedfrom;
      svn_revnum_t startrev;
      svn_revnum_t endrev;
      svn_boolean_t inheritable;
      const char *lastmergedfrom = NULL;

      *result = apr_hash_make(pool);
      pathranges = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

      while (got_row)
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

          SVN_ERR(svn_fs__sqlite_step(&got_row, stmt));
          lastmergedfrom = mergedfrom;
        }

      apr_hash_set(*result, mergedfrom, APR_HASH_KEY_STRING, pathranges);
    }

  SVN_ERR(svn_fs__sqlite_finalize(stmt));
  return SVN_NO_ERROR;
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


/* Helper for svn_fs_mergeinfo__get_mergeinfo().

   Update CACHE (and RESULT iff RESULT is non-null) with mergeinfo for
   PATH at REV, retrieved from DB.

   If INHERIT is svn_mergeinfo_explicit, then retrieve only explicit
   mergeinfo on PATH.  Else if it is svn_mergeinfo_nearest_ancestor,
   then retrieve the mergeinfo for PATH's parent, recursively.  Else
   if it is svn_mergeinfo_inherited, then:

      - If PATH had any explicit merges committed on or before REV,
        retrieve the explicit mergeinfo for PATH;

      - Else, retrieve mergeinfo for PATH's parent, recursively.

   Perform all allocations in POOL.  Due to the nature of APR pools,
   and the recursion in this function, invoke this function using a
   sub-pool.  To preserve RESULT, use mergeinfo_hash_dup() before
   clearing or destroying POOL.
*/
static svn_error_t *
get_mergeinfo_for_path(svn_fs_root_t *rev_root,
                       const char *path,
                       apr_hash_t *result,
                       apr_hash_t *cache,
                       svn_mergeinfo_inheritance_t inherit,
                       apr_pool_t *pool)
{
  const char *parent_path;
  apr_hash_t *parent_mergeinfo_hash;

  if (inherit != svn_mergeinfo_nearest_ancestor)
    {
      svn_string_t *my_mergeinfo_string;
      apr_hash_t *my_mergeinfo_hash;
      svn_error_t *err;

      /* Look up the explicit mergeinfo for PATH, starting with the
         cache, then moving on to the SQLite index. */
      my_mergeinfo_hash = apr_hash_get(cache, path, APR_HASH_KEY_STRING);
      if (my_mergeinfo_hash)
        {
          /* We already had a mergeinfo lookup attempt cached. */
          if (result && my_mergeinfo_hash != NEGATIVE_CACHE_RESULT)
            apr_hash_set(result, path, APR_HASH_KEY_STRING, my_mergeinfo_hash);
          return SVN_NO_ERROR;
        }

      /* XXXdsg: think about pools here */
      err = svn_fs_node_prop(&my_mergeinfo_string,
                             rev_root,
                             path,
                             SVN_PROP_MERGEINFO,
                             pool);
      if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          /* XXXdsg: 

             I believe this behavior is incorrect: this API really
             should error if it is asked about paths that don't exist!
             However, there is definitely code that expects this to be
             silently ignored.  Specifically, see
             libsvn_repos/log.c(get_merged_rev_mergeinfo), which
             indirectly does a mergeinfo lookup on "rev - 1".  I think
             that this should error, and the log code should have to
             handle that error; the only reason I'm not making that
             change is that I'm not sure what the behavior should be
             if there are multiple paths in the array and some of them
             do exist in the previous rev and others don't.
          */
          svn_error_clear(err);
          err = NULL;
          my_mergeinfo_string = NULL;
        }
      SVN_ERR(err);

      /* If we've got mergeinfo data, parse it from the db into a
         mergeinfo hash.  Either way, cache whether we found mergeinfo
         (although if we didn't and we're inheriting, we might
         overwrite the cache later). */
      if (my_mergeinfo_string)
        {
          apr_hash_t *mergeinfo_hash;
          SVN_ERR(svn_mergeinfo_parse(&mergeinfo_hash,
                                      my_mergeinfo_string->data,
                                      pool));
          apr_hash_set(cache, path, APR_HASH_KEY_STRING, mergeinfo_hash);
          if (result)
            apr_hash_set(result, path, APR_HASH_KEY_STRING, mergeinfo_hash);

          return SVN_NO_ERROR;
        }
      else
        apr_hash_set(cache, path, APR_HASH_KEY_STRING, NEGATIVE_CACHE_RESULT);
    }

  /* If we only care about mergeinfo that is on PATH itself, we're done. */
  if (inherit == svn_mergeinfo_explicit)
    return SVN_NO_ERROR;

  /* Either we haven't found mergeinfo yet and are allowed to inherit,
     or we were ignoring PATH's mergeinfo all along, so recurse up the
     tree. */
  
  /* It is possible we are already at the root.  */
  if (!*path)
    return SVN_NO_ERROR;

  parent_path = svn_path_dirname(path, pool);

  SVN_ERR(get_mergeinfo_for_path(rev_root, parent_path,
                                 NULL, cache, svn_mergeinfo_inherited,
                                 pool));
  parent_mergeinfo_hash = apr_hash_get(cache, parent_path,
                                       APR_HASH_KEY_STRING);
  if (parent_mergeinfo_hash == NEGATIVE_CACHE_RESULT)
    apr_hash_set(cache, path, APR_HASH_KEY_STRING, NULL);
  else if (parent_mergeinfo_hash)
    {
      /* Now translate the result for our parent to our path. */
      apr_hash_t *translated_mergeinfo_hash;
      const char *my_basename = svn_path_basename(path, pool);

      /* But first remove all non-inheritable revision ranges. */
      SVN_ERR(svn_mergeinfo_inheritable(&parent_mergeinfo_hash, 
                                        parent_mergeinfo_hash,
                                        NULL, SVN_INVALID_REVNUM,
                                        SVN_INVALID_REVNUM, pool));
      append_component_to_paths(&translated_mergeinfo_hash, 
                                parent_mergeinfo_hash,
                                my_basename,
                                pool);
      apr_hash_set(cache, path, APR_HASH_KEY_STRING, translated_mergeinfo_hash);
      if (result)
        apr_hash_set(result, path, APR_HASH_KEY_STRING, 
                     translated_mergeinfo_hash);
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
  apr_pool_t *subpool = svn_pool_create(pool);
  char *like_path;
  svn_boolean_t got_row;

  /* Get all paths under us. */
  SVN_ERR(svn_fs__sqlite_prepare(&stmt, db,
                                 "SELECT MAX(revision), path "
                                 "FROM mergeinfo_changed "
                                 "WHERE path LIKE ? AND revision <= ? "
                                 "GROUP BY path;"));

  like_path = apr_psprintf(subpool, "%s/%%", path);

  SVN_ERR(svn_fs__sqlite_bind_text(stmt, 1, like_path));
  SVN_ERR(svn_fs__sqlite_bind_int64(stmt, 2, rev));

  SVN_ERR(svn_fs__sqlite_step(&got_row, stmt));
  while (got_row)
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

      SVN_ERR(svn_fs__sqlite_step(&got_row, stmt));
    }

  SVN_ERR(svn_fs__sqlite_finalize(stmt));
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
get_mergeinfo(svn_fs_root_t *root,
              apr_hash_t **mergeinfo_hash,
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
      SVN_ERR(get_mergeinfo_for_path(root, path, result_hash, cache_hash,
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
                                svn_boolean_t include_descendents, /*XXXdsg: implement (or ignore this file) */
                                apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  subpool = svn_pool_create(pool);

  /* Retrieve a path -> mergeinfo hash mapping. */
  SVN_ERR(get_mergeinfo(root, mergeinfo, paths, inherit, pool));

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
  err = get_mergeinfo(root, mergeinfo, paths, svn_mergeinfo_inherited,
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
