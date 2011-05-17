/* rep-sharing.c --- the rep-sharing cache for fsfs
 *
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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

#include "fs_fs.h"
#include "fs.h"
#include "rep-cache.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_path.h"

#include "private/svn_sqlite.h"

#include "rep-cache-db.h"

/* A few magic values */
#define REP_CACHE_SCHEMA_FORMAT   1

static const char * const upgrade_sql[] = { NULL,
  REP_CACHE_DB_SQL
  };

/* These values are directly related to the statements contained in STATEMENTS
   below.  If you add something to that array, you'd better do the same here.
*/
enum statement_keys {
  STMT_GET_REP,
  STMT_SET_REP
};

static const char * const statements[] = {
  "select revision, offset, size, expanded_size "
  "from rep_cache "
  "where hash = ?1",

  "insert into rep_cache (hash, revision, offset, size, expanded_size) "
  "values (?1, ?2, ?3, ?4, ?5);",

  NULL
  };


static svn_error_t *
open_rep_cache(void *baton,
               apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *db_path;

  /* Be idempotent. */
  if (ffd->rep_cache_db)
    return SVN_NO_ERROR;

  /* Open (or create) the sqlite database.  It will be automatically
     closed when fs->pool is destoyed. */
  db_path = svn_path_join(fs->path, REP_CACHE_DB_NAME, pool);
  SVN_ERR(svn_sqlite__open(&ffd->rep_cache_db, db_path,
                           svn_sqlite__mode_rwcreate, statements,
                           REP_CACHE_SCHEMA_FORMAT,
                           upgrade_sql, fs->pool, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return svn_atomic__init_once(&ffd->rep_cache_db_opened,
                               open_rep_cache, fs, pool);
}

svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(ffd->rep_sharing_allowed);
  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  /* We only allow SHA1 checksums in this table. */
  if (checksum->kind != svn_checksum_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "rep_cache table.\n"));

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db, STMT_GET_REP));
  SVN_ERR(svn_sqlite__bindf(stmt, "s",
                            svn_checksum_to_cstring(checksum, pool)));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      *rep = apr_pcalloc(pool, sizeof(**rep));
      (*rep)->sha1_checksum = svn_checksum_dup(checksum, pool);
      (*rep)->revision = svn_sqlite__column_revnum(stmt, 0);
      (*rep)->offset = svn_sqlite__column_int64(stmt, 1);
      (*rep)->size = svn_sqlite__column_int64(stmt, 2);
      (*rep)->expanded_size = svn_sqlite__column_int64(stmt, 3);
    }
  else
    *rep = NULL;

  /* Sanity check. */
  if (*rep)
    {
      svn_revnum_t youngest;
      
      youngest = ffd->youngest_rev_cache;
      if (youngest < (*rep)->revision)
      {
        /* Stale cache. */
        SVN_ERR(svn_fs_fs__youngest_rev(&youngest, fs, pool));

        /* Fresh cache. */
        if (youngest < (*rep)->revision)
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   _("Youngest revision is r%ld, but "
                                     "rep-cache contains r%ld"),
                                   youngest, (*rep)->revision);
      }
    }

  return svn_sqlite__reset(stmt);
}

svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             svn_boolean_t reject_dup,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  representation_t *old_rep;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(ffd->rep_sharing_allowed);
  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  /* We only allow SHA1 checksums in this table. */
  if (rep->sha1_checksum == NULL)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "rep_cache table.\n"));

  /* Check to see if we already have a mapping for REP->SHA1_CHECKSUM.  If so,
     and the value is the same one we were about to write, that's
     cool -- just do nothing.  If, however, the value is *different*,
     that's a red flag!  */
  SVN_ERR(svn_fs_fs__get_rep_reference(&old_rep, fs, rep->sha1_checksum, pool));

  if (old_rep)
    {
      if ( reject_dup && ((old_rep->revision != rep->revision)
            || (old_rep->offset != rep->offset)
            || (old_rep->size != rep->size)
            || (old_rep->expanded_size != rep->expanded_size)) )
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                 apr_psprintf(pool,
                              _("Representation key for checksum '%%s' exists "
                                "in filesystem '%%s' with a different value "
                                "(%%ld,%%%s,%%%s,%%%s) than what we were about "
                                "to store (%%ld,%%%s,%%%s,%%%s)"),
                              APR_OFF_T_FMT, SVN_FILESIZE_T_FMT,
                              SVN_FILESIZE_T_FMT, APR_OFF_T_FMT,
                              SVN_FILESIZE_T_FMT, SVN_FILESIZE_T_FMT),
                 svn_checksum_to_cstring_display(rep->sha1_checksum, pool),
                 fs->path, old_rep->revision, old_rep->offset, old_rep->size,
                 old_rep->expanded_size, rep->revision, rep->offset, rep->size,
                 rep->expanded_size);
      else
        return SVN_NO_ERROR;
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db, STMT_SET_REP));
  SVN_ERR(svn_sqlite__bindf(stmt, "siiii",
                            svn_checksum_to_cstring(rep->sha1_checksum, pool),
                            (apr_int64_t) rep->revision,
                            (apr_int64_t) rep->offset,
                            (apr_int64_t) rep->size,
                            (apr_int64_t) rep->expanded_size));

  return svn_sqlite__insert(NULL, stmt);
}
