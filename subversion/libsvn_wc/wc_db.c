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
#include "svn_dirent_uri.h"
#include "svn_wc.h"

#include "wc.h"
#include "wc_db.h"
#include "adm_files.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"


#define NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")


/*
 * PARAMETER ASSERTIONS
 *
 * Every (semi-)public entrypoint in this file has a set of assertions on
 * the parameters passed into the function. Since this is a brand new API,
 * we want to make sure that everybody calls it properly. The original WC
 * code had years to catch stray bugs, but we do not have that luxury in
 * the wc-nb rewrite. Any extra assurances that we can find will be
 * welcome. The asserts will ensure we have no doubt about the values
 * passed into the function.
 *
 * Some parameters are *not* specifically asserted. Typically, these are
 * params that will be used immediately, so something like a NULL value
 * will be obvious.
 *
 * ### near 1.7 release, it would be a Good Thing to review the assertions
 * ### and decide if any can be removed or switched to assert() in order
 * ### to remove their runtime cost in the production release.
 *
 *
 * DATABASE OPERATIONS
 *
 * Each function should leave the database in a consistent state. If it
 * does *not*, then the implication is some other function needs to be
 * called to restore consistency. Subtle requirements like that are hard
 * to maintain over a long period of time, so this API will not allow it.
 */


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
                   const svn_checksum_t *checksum,
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
      const char *subdir_path = svn_dirent_join(pdh->base_dir, subdir,
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
  *path = svn_dirent_join_many(result_pool,
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

  SVN_ERR_ASSERT(svn_dirent_is_absolute(path));

  /* If the file is special, then we need to refer to the encapsulating
     directory instead, rather than resolving through a symlink to a
     file or directory. */
  SVN_ERR(svn_io_check_special_path(path, &kind, &special, scratch_pool));

  /* ### skip unknown and/or not-found paths? need to examine typical
     ### caller usage. */

  if (kind != svn_node_dir)
    {
      /* ### doesn't seem that we need to keep the original path */
      path = svn_dirent_dirname(path, scratch_pool);
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
  pdh->base_dir = svn_dirent_join(path, ".svn/pristine", result_pool);

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
                const char *local_abspath,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = new_db_state(mode, config, result_pool);

  return open_one_directory(*db, local_abspath, result_pool, scratch_pool);
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
svn_wc__db_version(int *version,
                   const char *path,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *format_file_path;

  /* First, try reading the wc.db file.  Instead of stat'ing the file to
     see if it exists, and then opening it, we just try opening it.  If we
     get any kind of an error, wrap that eith an ENOENT error and return. */
  err = svn_sqlite__get_schema_version(version,
                                       svn_wc__adm_child(path, "wc.db",
                                                         scratch_pool),
                                       scratch_pool);
  if (err
      && err->apr_err != SVN_ERR_SQLITE_ERROR
      && !APR_STATUS_IS_ENOENT(err->apr_err))
    return err;
  else if (!err)
    return SVN_NO_ERROR;

  /* Hmm, that didn't work.  Now try reading the format number from the
     entries file. */
  svn_error_clear(err);
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_ENTRIES, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err && err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT)
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  else if (!err)
    return SVN_NO_ERROR;

  /* Wow, another error; this must be a really old working copy!  Fall back
     to reading the format file. */
  svn_error_clear(err);
  /* Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_FORMAT, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);

  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
              || APR_STATUS_IS_ENOTDIR(err->apr_err)))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  else if (!err)
    return SVN_NO_ERROR;

  /* If we've gotten this far, all of the above checks have failed, so just
     bail. */
  return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                           _("'%s' is not a working copy"),
                           svn_dirent_local_style(path, scratch_pool));
}


svn_error_t *
svn_wc__db_txn_begin(svn_wc__db_t *db,
                     apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_txn_rollback(svn_wc__db_t *db,
                        apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_txn_commit(svn_wc__db_t *db,
                      apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_txn_rollback(db, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_directory(svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *repos_relpath,
                              const char *repos_root_url,
                              const char *repos_uuid,
                              svn_revnum_t revision,
                              const apr_hash_t *props,
                              svn_revnum_t changed_rev,
                              apr_time_t changed_date,
                              const char *changed_author,
                              const apr_array_header_t *children,
                              svn_depth_t depth,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(children != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_add_file(svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *repos_relpath,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         svn_revnum_t revision,
                         const apr_hash_t *props,
                         svn_revnum_t changed_rev,
                         apr_time_t changed_date,
                         const char *changed_author,
                         const svn_checksum_t *checksum,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(checksum != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_add_symlink(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *repos_relpath,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            svn_revnum_t revision,
                            const apr_hash_t *props,
                            svn_revnum_t changed_rev,
                            apr_time_t changed_date,
                            const char *changed_author,
                            const char *target,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(target != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_add_absent_node(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *repos_relpath,
                                const char *repos_root_url,
                                const char *repos_uuid,
                                svn_revnum_t revision,
                                svn_wc__db_kind_t kind,
                                apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_absent_dir
                 || kind == svn_wc__db_kind_absent_file
                 || kind == svn_wc__db_kind_absent_symlink);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_delete(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_info(svn_wc__db_kind_t *kind,
                         svn_revnum_t *revision,
                         const char **repos_relpath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         svn_revnum_t *changed_rev,
                         apr_time_t *changed_date,
                         const char **changed_author,
                         svn_depth_t *depth,
                         const svn_checksum_t **checksum,
                         svn_filesize_t *translated_size,
                         svn_boolean_t *switched,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_prop(const svn_string_t **propval,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *propname,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_props(apr_hash_t **props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_children(const apr_array_header_t **children,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_symlink_target(const char **target,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_get_handle(svn_wc__db_pdh_t **pdh,
                               svn_wc__db_t *db,
                               const char *local_dir_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  /* ### need to fix this up. we'll probably get called with a subdirectory
     ### of the path that we opened originally. that means we probably
     ### won't have the subdir in the hash table. need to be able to
     ### incrementally grow the hash of per-dir structures. */

  *pdh = apr_hash_get(db->dir_data, local_dir_abspath, APR_HASH_KEY_STRING);

  if (*pdh == NULL)
    {
      /* Oops. We haven't seen this WC directory before. Let's get it into
         our hash of per-directory information. */
      SVN_ERR(open_one_directory(db, local_dir_abspath,
                                 result_pool, scratch_pool));

      *pdh = apr_hash_get(db->dir_data, local_dir_abspath, APR_HASH_KEY_STRING);

      SVN_ERR_ASSERT(*pdh != NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_read(svn_stream_t **contents,
                         svn_wc__db_pdh_t *pdh,
                         const svn_checksum_t *checksum,
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
                          const svn_checksum_t *checksum,
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
svn_wc__db_pristine_get_tempdir(const char **temp_dir,
                                svn_wc__db_pdh_t *pdh,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_install(svn_wc__db_pdh_t *pdh,
                            const char *local_abspath,
                            const svn_checksum_t *checksum,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_check(svn_boolean_t *present,
                          int *refcount,
                          svn_wc__db_pdh_t *pdh,
                          const svn_checksum_t *checksum,
                          svn_wc__db_checkmode_t mode,
                          apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_repair(svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_incref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_decref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}

svn_error_t *
svn_wc__db_op_copy(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_copy_url(svn_wc__db_t *db,
                       const char *local_abspath,
                       const char *copyfrom_repos_relpath,
                       const char *copyfrom_root_url,
                       const char *copyfrom_uuid,
                       svn_revnum_t copyfrom_revision,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(copyfrom_repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(copyfrom_root_url));
  SVN_ERR_ASSERT(copyfrom_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(copyfrom_revision));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_directory(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_hash_t *props,
                            const apr_array_header_t *children,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(children != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_hash_t *props,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_hash_t *props,
                          const char *target,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(target != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_absent_node(svn_wc__db_t *db,
                              const char *local_abspath,
                              svn_wc__db_kind_t kind,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_absent_dir
                 || kind == svn_wc__db_kind_absent_file
                 || kind == svn_wc__db_kind_absent_symlink);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_set_prop(svn_wc__db_t *db,
                       const char *local_abspath,
                       const char *propname,
                       const svn_string_t *propval,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_hash_t *props,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_delete(svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_move(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_modified(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_to_changelist(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *changelist,
                                apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_mark_conflict(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_mark_resolved(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_revert(svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_depth_t depth,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_info(svn_wc__db_status_t *status,
                     svn_wc__db_kind_t *kind,
                     svn_revnum_t *revision,
                     const char **repos_relpath,
                     const char **repos_root_url,
                     const char **repos_uuid,
                     svn_revnum_t *changed_rev,
                     apr_time_t *changed_date,
                     const char **changed_author,
                     svn_depth_t *depth,
                     const svn_checksum_t **checksum,
                     svn_filesize_t *translated_size,
                     const char **changelist,
                     const char **original_repos_relpath,
                     const char **original_root_url,
                     const char **original_uuid,
                     svn_revnum_t *original_revision,
                     svn_boolean_t *text_mod,
                     svn_boolean_t *props_mod,
                     svn_boolean_t *base_shadowed,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_prop(const svn_string_t **propval,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const char *propname,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_props(apr_hash_t **props,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_pristine_props(apr_hash_t **props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_children(const apr_array_header_t **children,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_symlink_target(const char **target,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_global_relocate(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           const char *from_url,
                           const char *to_url,
                           svn_depth_t depth,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_global_commit(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_revnum_t new_revision,
                         apr_time_t new_date,
                         const char *new_author,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_date > 0);
  SVN_ERR_ASSERT(new_author != NULL);

  NOT_IMPLEMENTED();
}
