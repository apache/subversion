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
  /* Map a given working copy directory to its relevant data. */
  apr_hash_t *dir_data;
};

/**
 * This structure records all the information that we need to deal with
 * a given working copy directory.
 */
struct svn_wc__db_pdh_t {
  /* Root of the TEXT-BASE directory structure for the WORKING/ACTUAL files
     in this directory. */
  const char *base_dir;
};


static svn_error_t *
get_pristine_fname(const char **path,
                   svn_wc__db_pdh_t *pdh,
                   svn_checksum_t *checksum,
                   svn_boolean_t create_subdir,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *hexdigest = svn_checksum_to_cstring(checksum, scratch_pool);
  char subdir[3] = { 0 };

  /* We should have a valid checksum and (thus) a valid digest. */
  SVN_ERR_ASSERT(hexdigest != NULL);

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

  /* The file is located at DIR/.svn/pristine/XX/XXYYZZ... */
  *path = svn_path_join_many(scratch_pool,
                             pdh->base_dir, subdir, hexdigest, NULL);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_open_many(svn_wc__db_t **db,
                     const apr_array_header_t *paths,
                     svn_config_t *config,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  int i;

  *db = apr_pcalloc(result_pool, sizeof(**db));
  (*db)->dir_data = apr_hash_make(result_pool);

  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
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

      pdh = apr_hash_get((*db)->dir_data, path, APR_HASH_KEY_STRING);
      if (pdh != NULL)
        continue;  /* seen this directory already! */

      pdh = apr_pcalloc(result_pool, sizeof(*pdh));

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
      apr_hash_set((*db)->dir_data, path, APR_HASH_KEY_STRING, pdh);
    }

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

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_read(svn_stream_t **contents,
                         svn_wc__db_pdh_t *pdh,
                         svn_checksum_t *checksum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_file_t *fh;
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, FALSE /* create_subdir */,
                             result_pool, scratch_pool));

  SVN_ERR(svn_io_file_open(&fh, path,
                           APR_FOPEN_READ
                             | APR_FOPEN_BINARY,
                           APR_OS_DEFAULT, result_pool));
  *contents = svn_stream_from_aprfile2(fh, FALSE, result_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_write(svn_stream_t **contents,
                          svn_wc__db_pdh_t *pdh,
                          svn_checksum_t *checksum,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_file_t *fh;
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, TRUE /* create_subdir */,
                             result_pool, scratch_pool));

  SVN_ERR(svn_io_file_open(&fh, path,
                           APR_FOPEN_WRITE
                             | APR_FOPEN_CREATE
                             | APR_FOPEN_EXCL
                             | APR_FOPEN_BINARY,
                           APR_OS_DEFAULT, result_pool));

  *contents = svn_stream_from_aprfile2(fh, FALSE, result_pool);

  /* ### we should wrap the stream. count the bytes. at close, then we
     ### should write the count into the sqlite database. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_check(svn_boolean_t *present,
                          svn_filesize_t *actual_size,
                          int *refcount,
                          svn_wc__db_pdh_t *pdh,
                          svn_checksum_t *checksum,
                          apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(__FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_pristine_incref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(__FILE__, __LINE__, "Not implemented.");
}


svn_error_t *
svn_wc__db_pristine_decref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  return svn_error__malfunction(__FILE__, __LINE__, "Not implemented.");
}
