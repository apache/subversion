/*
 * wc_db.c :  manipulating the administrative database
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


#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"

#include "wc_db.h"

#include "svn_private_config.h"


struct svn_wc__db_t {
  /* What's the appropriate mode for this datastore? */
  svn_wc__db_openmode_t mode;

  /* We need the config whenever we run into a new WC directory, in order
     to figure out where we should look for the corresponding datastore. */
  svn_config_t *config;

  /* Map a given working copy directory to its relevant data. */
  apr_hash_t *dir_data;
};

/**
 * This structure records all the information that we need to deal with
 * a given working copy directory.
 */
struct svn_wc__db_pdh_t {
  /* This per-dir state is associated with this global state. */
  svn_wc__db_t *db;

  /* Root of the TEXT-BASE directory structure for the WORKING/ACTUAL files
     in this directory. */
  const char *base_dir;
};

/* ### since we're putting the pristine files per-dir, then we don't need
   ### to create subdirectories in order to keep the directory size down.
   ### when we can aggregate pristine files across dirs/wcs, then we will
   ### need to undo the SKIP. */
#define SVN__SKIP_SUBDIR


static svn_error_t *
get_pristine_fname(const char **path,
                   svn_wc__db_pdh_t *pdh,
                   svn_checksum_t *checksum,
                   svn_boolean_t create_subdir,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *hexdigest = svn_checksum_to_cstring(checksum, scratch_pool);
#ifndef SVN__SKIP_SUBDIR
  char subdir[3] = { 0 };
#endif

  /* We should have a valid checksum and (thus) a valid digest. */
  SVN_ERR_ASSERT(hexdigest != NULL);

#ifndef SVN__SKIP_SUBDIR
  /* Get the first two characters of the digest, for the subdir. */
  subdir[0] = hexdigest[0];
  subdir[1] = hexdigest[1];

  if (create_subdir)
    {
      const char *subdir_path = svn_path_join(pdh->base_dir, subdir,
                                              scratch_pool);
      svn_error_t *err;

      err = svn_io_dir_make(subdir_path, APR_OS_DEFAULT, scratch_pool);

      /* Whatever error may have occurred... ignore it. Typically, this
         will be "directory already exists", but if it is something
         *different*, then presumably another error will follow when we
         try to access the file within this (missing?) pristine subdir. */
      svn_error_clear(err);
    }
#endif

  /* The file is located at DIR/.svn/pristine/XX/XXYYZZ... */
  *path = svn_path_join_many(result_pool,
                             pdh->base_dir,
#ifndef SVN__SKIP_SUBDIR
                             subdir,
#endif
                             hexdigest,
                             NULL);
  return SVN_NO_ERROR;
}


static svn_error_t *
open_one_directory(svn_wc__db_t *db,
                   const char *path,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  svn_boolean_t special;
  svn_wc__db_pdh_t *pdh;

  /* If the file is special, then we need to refer to the encapsulating
     directory instead, rather than resolving through a symlink to a
     file or directory. */
  SVN_ERR(svn_io_check_special_path(path, &kind, &special, scratch_pool));

  /* ### skip unknown and/or not-found paths? need to examine typical
     ### caller usage. */

  if (kind != svn_node_dir)
    {
      /* ### doesn't seem that we need to keep the original path */
      path = svn_path_dirname(path, scratch_pool);
    }

  pdh = apr_hash_get(db->dir_data, path, APR_HASH_KEY_STRING);
  if (pdh != NULL)
    return SVN_NO_ERROR;  /* seen this directory already! */

  pdh = apr_palloc(result_pool, sizeof(*pdh));
  pdh->db = db;

  /* ### for now, every directory still has a .svn subdir, and a
     ### "pristine" subdir in there. later on, we'll alter the
     ### storage location/strategy */

  /* ### need to fix this to use a symbol for ".svn". we shouldn't need
     ### to use join_many since we know "/" is the separator for
     ### internal canonical paths */
  pdh->base_dir = svn_path_join(path, ".svn/pristine", result_pool);

  /* Make sure the key lasts as long as the hash. Note that if we did
     not call dirname(), then this path is the provided path, but we
     do not know its lifetime (nor does our API contract specify a
     requirement for the lifetime). */
  path = apr_pstrdup(result_pool, path);
  apr_hash_set(db->dir_data, path, APR_HASH_KEY_STRING, pdh);

  return SVN_NO_ERROR;
}


static svn_wc__db_t *
new_db_state(svn_wc__db_openmode_t mode,
             svn_config_t *config,
             apr_pool_t *result_pool)
{
  svn_wc__db_t *db = apr_palloc(result_pool, sizeof(*db));

  db->mode = mode;
  db->config = config;
  db->dir_data = apr_hash_make(result_pool);

  return db;
}


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_wc__db_openmode_t mode,
                const char *path,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = new_db_state(mode, config, result_pool);

  return open_one_directory(*db, path, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_open_many(svn_wc__db_t **db,
                     svn_wc__db_openmode_t mode,
                     const apr_array_header_t *paths,
                     svn_config_t *config,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  int i;

  *db = new_db_state(mode, config, result_pool);

  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      SVN_ERR(open_one_directory(*db, path, result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_txn_begin(svn_wc__db_t *db,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_txn_rollback(svn_wc__db_t *db,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_txn_commit(svn_wc__db_t *db,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_txn_rollback(db, result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_dirhandle(svn_wc__db_pdh_t **pdh,
                              svn_wc__db_t *db,
                              const char *dirpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  /* ### need to fix this up. we'll probably get called with a subdirectory
     ### of some dirpath that we opened originally. that means we probably
     ### won't have the subdir in the hash table. need to be able to
     ### incrementally grow the hash of per-dir structures. */

  *pdh = apr_hash_get(db->dir_data, dirpath, APR_HASH_KEY_STRING);

  if (*pdh == NULL)
    {
      /* Oops. We haven't seen this WC directory before. Let's get it into
         our hash of per-directory information. */
      SVN_ERR(open_one_directory(db, dirpath, result_pool, scratch_pool));

      *pdh = apr_hash_get(db->dir_data, dirpath, APR_HASH_KEY_STRING);

      SVN_ERR_ASSERT(*pdh != NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_read(svn_stream_t **contents,
                         svn_wc__db_pdh_t *pdh,
                         svn_checksum_t *checksum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, FALSE /* create_subdir */,
                             scratch_pool, scratch_pool));

  return svn_stream_open_readonly(contents, path, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_pristine_write(svn_stream_t **contents,
                          svn_wc__db_pdh_t *pdh,
                          svn_checksum_t *checksum,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, TRUE /* create_subdir */,
                             scratch_pool, scratch_pool));

  SVN_ERR(svn_stream_open_writable(contents, path, result_pool, scratch_pool));

  /* ### we should wrap the stream. count the bytes. at close, then we
     ### should write the count into the sqlite database. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_check(svn_boolean_t *present,
                          int *refcount,
                          svn_wc__db_pdh_t *pdh,
                          svn_checksum_t *checksum,
                          svn_wc__db_checkmode_t mode,
                          apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_pristine_repair(svn_wc__db_pdh_t *pdh,
                           svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_pristine_incref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_pristine_decref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.");
}
