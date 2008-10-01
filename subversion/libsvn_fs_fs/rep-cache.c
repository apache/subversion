/* rep-sharing.c --- the rep-sharing cache for fsfs
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"

#include "fs.h"
#include "rep-cache.h"

#include "../libsvn_fs/fs-loader.h"

/* ### Right now, the sqlite support is experiemental, so we need to guard
   this implementation. */
#ifdef ENABLE_SQLITE_TESTING

#include "svn_path.h"

#include "private/svn_sqlite.h"

/* A few magic values */
#define REP_CACHE_DB_NAME        "rep-cache.db"
#define REP_CACHE_SCHEMA_FORMAT   1

const char *upgrade_sql[] = { NULL,
  "pragma auto_vacuum = 1;"
  APR_EOL_STR
  "create table rep_cache (hash text not null,         "
  "                        revision integer not null,  "
  "                        offset integer not null);   "
  APR_EOL_STR
  "create unique index i_hash on rep_cache(hash);      "
  APR_EOL_STR
  };


/* APR cleanup function used to close the database when destroying the FS pool
   DATA should be the FS to to which this database belongs. */
static apr_status_t
cleanup_db_apr(void *data)
{
  svn_fs_t *fs = data;
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err = svn_sqlite__close(ffd->rep_cache, SVN_NO_ERROR);

  if (!err)
    return APR_SUCCESS;

  fs->warning(fs->warning_baton, err);
  svn_error_clear(err);

  return SVN_ERR_FS_CLEANUP;
}


svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *db_path;

  /* Open (or create) the sqlite database */
  db_path = svn_path_join(fs->path, REP_CACHE_DB_NAME, pool);
  SVN_ERR(svn_sqlite__open(&ffd->rep_cache, db_path, svn_sqlite__mode_rwcreate,
                           REP_CACHE_SCHEMA_FORMAT, upgrade_sql, fs->pool,
                           pool));

  apr_pool_cleanup_register(fs->pool, fs, cleanup_db_apr,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t have_row;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__prepare(&stmt, ffd->rep_cache,
                              "insert into rep_cache (hash, revision, offset) "
                              "values (?, ?, ?);", pool));
  SVN_ERR(svn_sqlite__bind_text(stmt, 1, svn_checksum_to_cstring(rep->checksum,
                                                                 pool)));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 2, rep->revision));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 3, rep->offset));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  return svn_sqlite__finalize(stmt);
}

#else
/* The checksum->rep mapping doesn't exist, so just pretend it exists, but is
   empty. */

svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool)
{
  *rep = NULL;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep_ref,
                             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

#endif /* ENABLE_SQLITE_TESTING */
