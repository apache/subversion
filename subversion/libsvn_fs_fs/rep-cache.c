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

const char SVN_MTD_CREATE_SQL[] = "pragma auto_vacuum = 1;"
  APR_EOL_STR
  "create table rep_cache (hash text not null,         "
  "                        revision integer not null,  "
  "                        offset integer not null);   "
  APR_EOL_STR
  "create unique index i_hash on rep_cache(hash);      "
  APR_EOL_STR
  "pragma user_version = " APR_STRINGIFY(REP_CACHE_SCHEMA_FORMAT) ";"
  APR_EOL_STR;


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

  printf("Warning! %d\n", err->apr_err);
  fs->warning(fs->warning_baton, err);
  svn_error_clear(err);

  return SVN_ERR_FS_CLEANUP;
}

/* Return SVN_ERR_FS_GENERAL if the schema doesn't exist,
   SVN_ERR_FS_UNSUPPORTED_FORMAT if the schema format is invalid, or
   SVN_ERR_FS_SQLITE_ERROR if an sqlite error occurs during
   validation.  Return SVN_NO_ERROR if everything is okay. */
static svn_error_t *
check_format(svn_sqlite__db_t *db,
             apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__prepare(&stmt, db, "pragma user_version;", pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      /* Validate that the schema exists as expected and that the
         schema and repository versions match. */
      int schema_format = svn_sqlite__column_int(stmt, 0);
      if (schema_format == REP_CACHE_SCHEMA_FORMAT)
        {
          err = SVN_NO_ERROR;
        }
      else if (schema_format == 0)
        {
          /* This is likely a freshly-created database in which the
             rep caching schema doesn't yet exist. */
          err = svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                                 _("Rep cache schema format not set"));
        }
      else if (schema_format > REP_CACHE_SCHEMA_FORMAT)
        {
          err = svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                                  _("Rep cache schema format %d "
                                    "not recognized"), schema_format);
        }
      /* else, we may one day want to perform a schema migration. */

      SVN_ERR(svn_sqlite__finalize(stmt));
    }
  return err;
}

svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  const char *db_path;

  /* Open (or create) the sqlite database */
  db_path = svn_path_join(fs->path, REP_CACHE_DB_NAME, pool);
  SVN_ERR(svn_sqlite__open(&ffd->rep_cache, db_path, svn_sqlite__mode_rwcreate,
                           0, NULL, fs->pool, pool));

  apr_pool_cleanup_register(fs->pool, fs, cleanup_db_apr,
                            apr_pool_cleanup_null);

  /* Check our format and create the table if needed. */
  err = check_format(ffd->rep_cache, pool);
  if (err && err->apr_err == SVN_ERR_FS_GENERAL)
    {
      /* Assume that we've just created an empty rep cache by way of (likely
         from accessing a pre-1.6 repository), and need to create the rep
         cache schema. */
      svn_error_clear(err);
      err = svn_sqlite__exec(ffd->rep_cache, SVN_MTD_CREATE_SQL);
    }

  return err;
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
                             representation_t *rep_ref,
                             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
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
