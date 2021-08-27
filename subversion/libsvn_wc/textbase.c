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
#include "workqueue.h"

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
  const svn_checksum_t *target_checksum;

  SVN_ERR(svn_wc__db_read_pristine_info(&status, &kind, NULL, NULL, NULL,
                                        NULL, &checksum, NULL, NULL,
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

  if (textbase_checksum)
    target_checksum = textbase_checksum;
  else
    target_checksum = checksum;

  if (!target_checksum)
    {
      *contents_p = NULL;
      return SVN_NO_ERROR;
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
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_pristine_prepare_install(stream_p, install_data_p,
                                              sha1_checksum_p,
                                              md5_checksum_p,
                                              db, local_abspath,
                                              result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
