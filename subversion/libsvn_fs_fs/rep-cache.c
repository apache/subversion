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

#include "svn_path.h"

#include "private/svn_sqlite.h"

/* A few magic values */
#define REP_CACHE_SCHEMA_FORMAT   1

static const char * const upgrade_sql[] = { NULL,
  "pragma auto_vacuum = 1;"
  APR_EOL_STR
  "create table rep_cache (hash text not null primary key,   "
  "                        revision integer not null,        "
  "                        offset integer not null,          "
  "                        size integer not null,            "
  "                        expanded_size integer not null,   "
  "                        reuse_count integer not null);    "
  APR_EOL_STR
  };


/* APR cleanup function used to close the database when destroying the FS pool
   DATA should be the FS to to which this database belongs. */
static apr_status_t
cleanup_db_apr(void *data)
{
  svn_fs_t *fs = data;
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;

  if (ffd->rep_cache.get_rep_stmt) 
    {
      err = svn_sqlite__finalize(ffd->rep_cache.get_rep_stmt);
      if (err)
        goto err_cleanup;
    }

  if (ffd->rep_cache.set_rep_stmt) 
    {
      err = svn_sqlite__finalize(ffd->rep_cache.set_rep_stmt);
      if (err)
        goto err_cleanup;
    }

  if (ffd->rep_cache.inc_select_stmt)
    {
      err = svn_sqlite__finalize(ffd->rep_cache.inc_select_stmt);
      if (err)
        goto err_cleanup;
    }

  if (ffd->rep_cache.inc_update_stmt)
    {
      err = svn_sqlite__finalize(ffd->rep_cache.inc_update_stmt);
      if (err)
        goto err_cleanup;
    }

  err = svn_sqlite__close(ffd->rep_cache.db, SVN_NO_ERROR);
  if (err)
    goto err_cleanup;

  return APR_SUCCESS;

  err_cleanup:
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
  SVN_ERR(svn_sqlite__open(&ffd->rep_cache.db, db_path,
                           svn_sqlite__mode_rwcreate, REP_CACHE_SCHEMA_FORMAT,
                           upgrade_sql, fs->pool, pool));

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
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t have_row;

  if (!ffd->rep_cache.get_rep_stmt)
    SVN_ERR(svn_sqlite__prepare(&ffd->rep_cache.get_rep_stmt, ffd->rep_cache.db,
                  "select revision, offset, size, expanded_size from rep_cache "
                  "where hash = :1", fs->pool));

  SVN_ERR(svn_sqlite__bind_text(ffd->rep_cache.get_rep_stmt, 1,
                                svn_checksum_to_cstring(checksum, pool)));

  SVN_ERR(svn_sqlite__step(&have_row, ffd->rep_cache.get_rep_stmt));
  if (have_row)
    {
      *rep = apr_pcalloc(pool, sizeof(**rep));
      (*rep)->checksum = svn_checksum_dup(checksum, pool);
      (*rep)->revision = svn_sqlite__column_revnum(ffd->rep_cache.get_rep_stmt,
                                                   0);
      (*rep)->offset = svn_sqlite__column_int(ffd->rep_cache.get_rep_stmt, 1);
      (*rep)->size = svn_sqlite__column_int(ffd->rep_cache.get_rep_stmt, 2);
      (*rep)->expanded_size = svn_sqlite__column_int(ffd->rep_cache.get_rep_stmt,
                                                     3);
    }
  else
    *rep = NULL;

  return svn_sqlite__reset(ffd->rep_cache.get_rep_stmt);
}

svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             svn_boolean_t reject_dup,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t have_row;
  representation_t *old_rep;

  /* Check to see if we already have a mapping for REP->CHECKSUM.  If so,
     and the value is the same one we were about to write, that's
     cool -- just do nothing.  If, however, the value is *different*,
     that's a red flag!  */
  SVN_ERR(svn_fs_fs__get_rep_reference(&old_rep, fs, rep->checksum, pool));

  if (old_rep)
    {
      if ( reject_dup && ((old_rep->revision != rep->revision)
            || (old_rep->offset != rep->offset)
            || (old_rep->size != rep->size)
            || (old_rep->expanded_size != rep->expanded_size)) )
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                 apr_psprintf(pool,
                              _("Representation key for checksum '%%s' exists "
                                "in filesystem '%%s', with different value "
                                "(%%ld,%%%s,%%%s,%%%s) than what we were about"
                                " to store(%%ld,%%%s,%%%s,%%%s)"),
                              APR_OFF_T_FMT, SVN_FILESIZE_T_FMT,
                              SVN_FILESIZE_T_FMT, APR_OFF_T_FMT,
                              SVN_FILESIZE_T_FMT, SVN_FILESIZE_T_FMT),
                 svn_checksum_to_cstring_display(rep->checksum, pool),
                 fs->path, old_rep->revision, old_rep->offset, old_rep->size,
                 old_rep->expanded_size, rep->revision, rep->offset, rep->size,
                 rep->expanded_size);
      else
        return SVN_NO_ERROR;
    }
    
  if (!ffd->rep_cache.set_rep_stmt)
    SVN_ERR(svn_sqlite__prepare(&ffd->rep_cache.set_rep_stmt, ffd->rep_cache.db,
                  "insert into rep_cache (hash, revision, offset, size, "
                  "expanded_size, reuse_count) "
                  "values (:1, :2, :3, :4, :5, 0);", fs->pool));

  SVN_ERR(svn_sqlite__bind_text(ffd->rep_cache.set_rep_stmt, 1,
                                svn_checksum_to_cstring(rep->checksum, pool)));
  SVN_ERR(svn_sqlite__bind_int64(ffd->rep_cache.set_rep_stmt, 2, rep->revision));
  SVN_ERR(svn_sqlite__bind_int64(ffd->rep_cache.set_rep_stmt, 3, rep->offset));
  SVN_ERR(svn_sqlite__bind_int64(ffd->rep_cache.set_rep_stmt, 4, rep->size));
  SVN_ERR(svn_sqlite__bind_int64(ffd->rep_cache.set_rep_stmt, 5,
                                 rep->expanded_size));

  SVN_ERR(svn_sqlite__step(&have_row, ffd->rep_cache.set_rep_stmt));
  return svn_sqlite__reset(ffd->rep_cache.set_rep_stmt);
}

svn_error_t *
svn_fs_fs__inc_rep_reuse(svn_fs_t *fs,
                         representation_t *rep,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t have_row;

  /* Fetch the current count. */
  if (!ffd->rep_cache.inc_select_stmt)
    SVN_ERR(svn_sqlite__prepare(&ffd->rep_cache.inc_select_stmt,
                  ffd->rep_cache.db,
                  "select reuse_count from rep_cache where hash = :1",
                  fs->pool));

  SVN_ERR(svn_sqlite__bind_text(ffd->rep_cache.inc_select_stmt, 1,
                                svn_checksum_to_cstring(rep->checksum, pool)));
  SVN_ERR(svn_sqlite__step(&have_row, ffd->rep_cache.inc_select_stmt));

  if (!have_row)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Representation for hash '%s' not found"),
                             svn_checksum_to_cstring_display(rep->checksum,
                                                             pool));

  rep->reuse_count =
           svn_sqlite__column_int(ffd->rep_cache.inc_select_stmt, 0) + 1;
  SVN_ERR(svn_sqlite__reset(ffd->rep_cache.inc_select_stmt));

  /* Update the reuse_count. */
  if (!ffd->rep_cache.inc_update_stmt)
    SVN_ERR(svn_sqlite__prepare(&ffd->rep_cache.inc_update_stmt,
                         ffd->rep_cache.db,
                         "update rep_cache set reuse_count = :1 where hash = :2",
                         fs->pool));

  SVN_ERR(svn_sqlite__bind_int64(ffd->rep_cache.inc_update_stmt, 1,
                                 rep->reuse_count));
  SVN_ERR(svn_sqlite__bind_text(ffd->rep_cache.inc_update_stmt, 2,
                                svn_checksum_to_cstring(rep->checksum, pool)));
  SVN_ERR(svn_sqlite__step_done(ffd->rep_cache.inc_update_stmt));

  return svn_sqlite__reset(ffd->rep_cache.inc_update_stmt);
}
