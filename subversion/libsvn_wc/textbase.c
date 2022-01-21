/*
 * textbase.c: working with text-bases
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "svn_dirent_uri.h"

#include "textbase.h"
#include "wc.h"
#include "translate.h"
#include "workqueue.h"

static svn_error_t *
compare_and_verify(svn_boolean_t *modified_p,
                   svn_wc__db_t *db,
                   const char *versioned_file_abspath,
                   svn_filesize_t versioned_file_size,
                   const svn_checksum_t *pristine_checksum,
                   svn_boolean_t has_props,
                   svn_boolean_t props_mod,
                   apr_pool_t *scratch_pool)
{
  svn_subst_eol_style_t eol_style;
  const char *eol_str;
  apr_hash_t *keywords;
  svn_boolean_t special = FALSE;
  svn_boolean_t need_translation;
  svn_stream_t *v_stream; /* versioned_file */
  svn_checksum_t *v_checksum;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_file_abspath));

  if (props_mod)
    has_props = TRUE; /* Maybe it didn't have properties; but it has now */

  if (has_props)
    {
      SVN_ERR(svn_wc__get_translate_info(&eol_style, &eol_str,
                                         &keywords,
                                         &special,
                                         db, versioned_file_abspath, NULL,
                                         TRUE, scratch_pool, scratch_pool));

      if (eol_style == svn_subst_eol_style_unknown)
        return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

      need_translation = svn_subst_translation_required(eol_style, eol_str,
                                                        keywords, special,
                                                        TRUE);
    }
  else
    need_translation = FALSE;

  if (! need_translation)
    {
      svn_filesize_t pristine_size;

      SVN_ERR(svn_wc__db_pristine_read(NULL, &pristine_size, db,
                                       versioned_file_abspath, pristine_checksum,
                                       scratch_pool, scratch_pool));

      if (versioned_file_size != pristine_size)
        {
          *modified_p = TRUE;

          return SVN_NO_ERROR;
        }
    }

  /* ### Other checks possible? */

  /* Reading files is necessary. */
  if (special && need_translation)
    {
      SVN_ERR(svn_subst_read_specialfile(&v_stream, versioned_file_abspath,
                                          scratch_pool, scratch_pool));
    }
  else
    {
      /* We don't use APR-level buffering because the comparison function
       * will do its own buffering. */
      apr_file_t *file;
      err = svn_io_file_open(&file, versioned_file_abspath, APR_READ,
                             APR_OS_DEFAULT, scratch_pool);
      /* Convert EACCESS on working copy path to WC specific error code. */
      if (err && APR_STATUS_IS_EACCES(err->apr_err))
        return svn_error_create(SVN_ERR_WC_PATH_ACCESS_DENIED, err, NULL);
      else
        SVN_ERR(err);
      v_stream = svn_stream_from_aprfile2(file, FALSE, scratch_pool);

      if (need_translation)
        {
          const char *pristine_eol_str;

          if (eol_style == svn_subst_eol_style_native)
            pristine_eol_str = SVN_SUBST_NATIVE_EOL_STR;
          else
            pristine_eol_str = eol_str;

          /* Wrap file stream to detranslate into normal form,
           * "repairing" the EOL style if it is inconsistent. */
          v_stream = svn_subst_stream_translated(v_stream,
                                                 pristine_eol_str,
                                                 TRUE /* repair */,
                                                 keywords,
                                                 FALSE /* expand */,
                                                 scratch_pool);
        }
    }

  /* Get checksum of detranslated (normalized) content. */
  err = svn_stream_contents_checksum(&v_checksum, v_stream,
                                     pristine_checksum->kind,
                                     scratch_pool, scratch_pool);
  /* Convert EACCESS on working copy path to WC specific error code. */
  if (err && APR_STATUS_IS_EACCES(err->apr_err))
    return svn_error_create(SVN_ERR_WC_PATH_ACCESS_DENIED, err, NULL);
  else
    SVN_ERR(err);

  *modified_p = (! svn_checksum_match(v_checksum, pristine_checksum));

  return SVN_NO_ERROR;
}

static svn_error_t *
check_file_modified(svn_boolean_t *modified_p,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_filesize_t recorded_size,
                    apr_time_t recorded_time,
                    const svn_checksum_t *pristine_checksum,
                    svn_boolean_t has_props,
                    svn_boolean_t props_mod,
                    apr_pool_t *scratch_pool)
{
  const svn_io_dirent2_t *dirent;
  svn_boolean_t modified;

  SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, TRUE,
                              scratch_pool, scratch_pool));

  if (dirent->kind == svn_node_file &&
      dirent->filesize == recorded_size &&
      dirent->mtime == recorded_time)
    {
      modified = FALSE;
    }
  else if (dirent->kind == svn_node_file)
    {
      SVN_ERR(compare_and_verify(&modified, db, local_abspath,
                                 dirent->filesize, pristine_checksum,
                                 has_props, props_mod, scratch_pool));
      if (!modified)
        {
          svn_boolean_t own_lock;

          /* The timestamp is missing or "broken" so "repair" it if we can. */
          SVN_ERR(svn_wc__db_wclock_owns_lock(&own_lock, db, local_abspath,
                                              FALSE, scratch_pool));
          if (own_lock)
            SVN_ERR(svn_wc__db_global_record_fileinfo(db, local_abspath,
                                                      dirent->filesize,
                                                      dirent->mtime,
                                                      scratch_pool));
        }
    }
  else
    {
      modified = TRUE;
    }

  *modified_p = modified;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_textbase(svn_stream_t **contents_p,
              svn_wc__db_t *db,
              const char *local_abspath,
              const svn_checksum_t *textbase_checksum,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const svn_checksum_t *checksum;
  svn_filesize_t recorded_size;
  apr_time_t recorded_time;
  svn_boolean_t have_props;
  svn_boolean_t props_mod;
  const svn_checksum_t *target_checksum;

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &checksum, NULL, NULL,
                               NULL, NULL, NULL, NULL, &recorded_size,
                               &recorded_time, NULL, NULL, NULL,
                               &have_props, &props_mod, NULL, NULL,
                               NULL, db, local_abspath,
                               scratch_pool, scratch_pool));

  /* Sanity */
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Can only get the pristine contents of files; "
                               "'%s' is not a file"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (status == svn_wc__db_status_not_present)
    {
      /* We know that the delete of this node has been committed.
         This should be the same as if called on an unknown path. */
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("Cannot get the pristine contents of '%s' "
                                 "because its delete is already committed"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  else if (status == svn_wc__db_status_server_excluded ||
           status == svn_wc__db_status_excluded ||
           status == svn_wc__db_status_incomplete)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot get the pristine contents of '%s' "
                                 "because it has an unexpected status"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  else if (status == svn_wc__db_status_deleted)
    {
      SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, NULL, NULL, NULL,
                                            NULL, &checksum, NULL, &have_props,
                                            NULL, db, local_abspath,
                                            scratch_pool, scratch_pool));
      recorded_size = SVN_INVALID_FILESIZE;
      recorded_time = -1;
      props_mod = TRUE;
    }

  if (textbase_checksum)
    target_checksum = textbase_checksum;
  else
    target_checksum = checksum;

  if (!target_checksum)
    {
      *contents_p = NULL;
      return SVN_NO_ERROR;
    }

  if (checksum && svn_checksum_match(checksum, target_checksum))
    {
      svn_boolean_t modified;

      SVN_ERR(check_file_modified(&modified, db, local_abspath, recorded_size,
                                  recorded_time, target_checksum, have_props,
                                  props_mod, scratch_pool));
      if (!modified)
        {
          SVN_ERR(svn_wc__internal_translated_stream(contents_p, db,
                                                     local_abspath,
                                                     local_abspath,
                                                     SVN_WC_TRANSLATE_TO_NF,
                                                     result_pool,
                                                     scratch_pool));

          return SVN_NO_ERROR;
        }
    }

  SVN_ERR(svn_wc__db_pristine_read(contents_p, NULL, db, local_abspath,
                                   target_checksum, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__textbase_get_contents(svn_stream_t **contents_p,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const svn_checksum_t *checksum,
                              svn_boolean_t ignore_enoent,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_stream_t *contents;

  SVN_ERR(open_textbase(&contents, db, local_abspath, checksum,
                        result_pool, scratch_pool));

  if (!contents && ignore_enoent)
    {
      *contents_p = NULL;
      return SVN_NO_ERROR;
    }
  else if (!contents)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Node '%s' has no pristine text"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  *contents_p = contents;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__textbase_setaside(const char **result_abspath_p,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          const svn_checksum_t *checksum,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_stream_t *contents;
  const char *tmpdir_abspath;
  svn_stream_t *tmpstream;
  const char *tmpfile_abspath;
  svn_error_t *err;

  SVN_ERR(open_textbase(&contents, db, local_abspath, checksum,
                        scratch_pool, scratch_pool));

  if (!contents)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Node '%s' has no pristine text"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath,
                                         db, local_abspath,
                                         scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(&tmpstream, &tmpfile_abspath, tmpdir_abspath,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));
  err = svn_stream_copy3(contents, tmpstream, cancel_func, cancel_baton,
                         scratch_pool);
  if (err)
    {
      return svn_error_compose_create(
               err,
               svn_io_remove_file2(tmpfile_abspath, TRUE, scratch_pool));
    }

  *result_abspath_p = tmpfile_abspath;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__textbase_setaside_wq(const char **result_abspath_p,
                             svn_skel_t **cleanup_work_item_p,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             const svn_checksum_t *checksum,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_stream_t *contents;
  const char *tmpdir_abspath;
  svn_stream_t *tmpstream;
  const char *tmpfile_abspath;
  svn_skel_t *work_item;
  svn_error_t *err;

  SVN_ERR(open_textbase(&contents, db, local_abspath, checksum,
                        scratch_pool, scratch_pool));

  if (!contents)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Node '%s' has no pristine text"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath,
                                         db, local_abspath,
                                         scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(&tmpstream, &tmpfile_abspath, tmpdir_abspath,
                                 svn_io_file_del_none,
                                 result_pool, scratch_pool));
  err = svn_wc__wq_build_file_remove(&work_item, db,
                                     local_abspath, tmpfile_abspath,
                                     result_pool, scratch_pool);
  if (!err)
    err = svn_stream_copy3(contents, tmpstream, cancel_func, cancel_baton,
                           scratch_pool);

  if (err)
    {
      return svn_error_compose_create(
               err,
               svn_io_remove_file2(tmpfile_abspath, TRUE, scratch_pool));
    }

  *result_abspath_p = tmpfile_abspath;
  *cleanup_work_item_p = work_item;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__textbase_prepare_install(svn_stream_t **stream_p,
                                 svn_wc__db_install_data_t **install_data_p,
                                 svn_checksum_t **sha1_checksum_p,
                                 svn_checksum_t **md5_checksum_p,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_boolean_t hydrated,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_pristine_prepare_install(stream_p, install_data_p,
                                              sha1_checksum_p,
                                              md5_checksum_p,
                                              db, local_abspath, hydrated,
                                              result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* A baton for use with textbase_walk_cb() and textbase_sync_cb(). */
typedef struct textbase_sync_baton_t
{
  svn_wc__db_t *db;
  svn_wc__textbase_hydrate_cb_t hydrate_callback;
  void *hydrate_baton;
} textbase_sync_baton_t;

/* Implements svn_wc__db_textbase_walk_cb_t. */
static svn_error_t *
textbase_walk_cb(svn_boolean_t *referenced_p,
                 void *baton,
                 const char *local_abspath,
                 int op_depth,
                 const svn_checksum_t *checksum,
                 svn_boolean_t have_props,
                 svn_boolean_t props_mod,
                 svn_filesize_t recorded_size,
                 apr_time_t recorded_time,
                 int max_op_depth,
                 apr_pool_t *scratch_pool)
{
  textbase_sync_baton_t *b = baton;
  svn_boolean_t referenced;

  if (op_depth < max_op_depth)
    {
      /* Pin the text-base with working changes. */
      referenced = TRUE;
    }
  else
    {
      svn_boolean_t modified;

      SVN_ERR(check_file_modified(&modified, b->db, local_abspath,
                                  recorded_size, recorded_time, checksum,
                                  have_props, props_mod, scratch_pool));
      /* Pin the text-base for modified files. */
      referenced = modified;
    }

  *referenced_p = referenced;
  return SVN_NO_ERROR;
}

/* Implements svn_wc__db_textbase_hydrate_cb_t. */
static svn_error_t *
textbase_hydrate_cb(void *baton,
                    const char *repos_root_url,
                    const char *repos_relpath,
                    svn_revnum_t revision,
                    svn_stream_t *contents,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  textbase_sync_baton_t *b = baton;

  SVN_ERR(b->hydrate_callback(b->hydrate_baton, repos_root_url,
                              repos_relpath, revision, contents,
                              cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__textbase_sync(svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      svn_boolean_t allow_hydrate,
                      svn_boolean_t allow_dehydrate,
                      svn_wc__textbase_hydrate_cb_t hydrate_callback,
                      void *hydrate_baton,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  textbase_sync_baton_t baton = {0};

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  baton.db = wc_ctx->db;
  baton.hydrate_callback = hydrate_callback;
  baton.hydrate_baton = hydrate_baton;

  SVN_ERR(svn_wc__db_textbase_walk(wc_ctx->db, local_abspath,
                                   textbase_walk_cb, &baton,
                                   cancel_func, cancel_baton,
                                   scratch_pool));

  SVN_ERR(svn_wc__db_textbase_sync(wc_ctx->db, local_abspath,
                                   allow_hydrate, allow_dehydrate,
                                   textbase_hydrate_cb, &baton,
                                   cancel_func, cancel_baton,
                                   scratch_pool));

  return SVN_NO_ERROR;
}
