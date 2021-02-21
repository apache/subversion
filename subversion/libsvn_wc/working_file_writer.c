/*
 * working_file_writer.c :  utility to prepare and install working files
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

#include "svn_props.h"
#include "svn_subst.h"
#include "svn_time.h"
#include "svn_path.h"

#include "private/svn_io_private.h"

#include "working_file_writer.h"

struct svn_wc__working_file_writer_t
{
  apr_pool_t *pool;
  const char *tmp_abspath;
  svn_boolean_t is_special;
  svn_stream_t *install_stream;
  svn_stream_t *write_stream;
};

static apr_status_t
cleanup_file_writer(void *baton)
{
  svn_wc__working_file_writer_t *writer = baton;

  if (writer->install_stream)
    {
      svn_error_clear(svn_stream__install_delete(writer->install_stream,
                                                 writer->pool));
      writer->install_stream = NULL;
    }

  return APR_SUCCESS;
}

svn_error_t *
svn_wc__working_file_writer_open(svn_wc__working_file_writer_t **writer_p,
                                 const char *tmp_abspath,
                                 apr_time_t final_mtime,
                                 apr_hash_t *props,
                                 svn_revnum_t changed_rev,
                                 apr_time_t changed_date,
                                 const char *changed_author,
                                 svn_boolean_t has_lock,
                                 svn_boolean_t is_added,
                                 const char *repos_root_url,
                                 const char *repos_relpath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__working_file_writer_t *writer;
  const char *url;
  svn_boolean_t special;
  svn_boolean_t executable;
  svn_boolean_t needs_lock;
  const char *keywords_propval;
  apr_hash_t *keywords;
  const char *eol_propval;
  svn_subst_eol_style_t eol_style;
  const char *eol;
  svn_stream_t *install_stream;
  svn_stream_t *write_stream;

  url = svn_path_url_add_component2(repos_root_url, repos_relpath, scratch_pool);

  special = svn_prop_get_value(props, SVN_PROP_SPECIAL) != NULL;
  executable = svn_prop_get_value(props, SVN_PROP_EXECUTABLE) != NULL;
  needs_lock = svn_prop_get_value(props, SVN_PROP_NEEDS_LOCK) != NULL;

  eol_propval = svn_prop_get_value(props, SVN_PROP_EOL_STYLE);
  svn_subst_eol_style_from_value(&eol_style, &eol, eol_propval);

  keywords_propval = svn_prop_get_value(props, SVN_PROP_KEYWORDS);
  if (keywords_propval)
    {
      SVN_ERR(svn_subst_build_keywords3(&keywords, keywords_propval,
                                        apr_psprintf(scratch_pool, "%ld",
                                                     changed_rev),
                                        url, repos_root_url, changed_date,
                                        changed_author, scratch_pool));
      if (apr_hash_count(keywords) <= 0)
        keywords = NULL;
    }
  else
    keywords = NULL;

  SVN_ERR(svn_stream__create_for_install(&install_stream, tmp_abspath,
                                         result_pool, scratch_pool));

  if (needs_lock && !is_added && !has_lock)
    svn_stream__install_set_read_only(install_stream, TRUE);
  if (executable)
    svn_stream__install_set_executable(install_stream, TRUE);
  if (final_mtime >= 0)
    svn_stream__install_set_affected_time(install_stream, final_mtime);

  write_stream = install_stream;

  if (svn_subst_translation_required(eol_style, eol, keywords,
                                     FALSE /* special */,
                                     TRUE /* force_eol_check */))
    {
      write_stream = svn_subst_stream_translated(write_stream,
                                                 eol,
                                                 TRUE /* repair */,
                                                 keywords,
                                                 TRUE /* expand */,
                                                 result_pool);
    }

  writer = apr_pcalloc(result_pool, sizeof(*writer));
  writer->pool = result_pool;
  writer->tmp_abspath = apr_pstrdup(result_pool, tmp_abspath);
  writer->is_special = special;
  writer->install_stream = install_stream;
  writer->write_stream = write_stream;

  apr_pool_cleanup_register(result_pool, writer, cleanup_file_writer,
                            apr_pool_cleanup_null);

  *writer_p = writer;
  return SVN_NO_ERROR;
}

svn_stream_t *
svn_wc__working_file_writer_get_stream(svn_wc__working_file_writer_t *writer)
{
  return writer->write_stream;
}

svn_error_t *
svn_wc__working_file_writer_finalize(apr_time_t *mtime_p,
                                     apr_off_t *size_p,
                                     svn_wc__working_file_writer_t *writer,
                                     apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream__install_finalize(mtime_p, size_p, writer->install_stream,
                                       scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__working_file_writer_install(svn_wc__working_file_writer_t *writer,
                                    const char *target_abspath,
                                    apr_pool_t *scratch_pool)
{
  if (writer->is_special)
    {
      const char *temp_path;
      svn_stream_t *src_stream;
      svn_stream_t *dst_stream;

      /* Install the current contents to a temporary file, and use it to
         create the resulting special file. */
      SVN_ERR(svn_io_open_unique_file3(NULL, &temp_path, writer->tmp_abspath,
                                       svn_io_file_del_on_pool_cleanup,
                                       scratch_pool, scratch_pool));
      SVN_ERR(svn_stream__install_stream(writer->install_stream, temp_path,
                                         TRUE, scratch_pool));
      writer->install_stream = NULL;

      /* When this stream is closed, the resulting special file will
         atomically be created/moved into place at LOCAL_ABSPATH. */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, target_abspath,
                                           scratch_pool, scratch_pool));
      SVN_ERR(svn_stream_open_readonly(&src_stream, temp_path, scratch_pool,
                                       scratch_pool));
      SVN_ERR(svn_stream_copy3(src_stream, dst_stream, NULL, NULL,
                               scratch_pool));
      SVN_ERR(svn_io_remove_file2(temp_path, TRUE, scratch_pool));

      return SVN_NO_ERROR;
    }
  else
    {
      /* With a single db we might want to install files in a missing directory.
         Simply trying this scenario on error won't do any harm and at least
         one user reported this problem on IRC. */
      SVN_ERR(svn_stream__install_stream(writer->install_stream,
                                         target_abspath, TRUE,
                                         scratch_pool));
      writer->install_stream = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__working_file_writer_close(svn_wc__working_file_writer_t *writer)
{
  if (writer->install_stream)
    {
      svn_stream_t *stream = writer->install_stream;
      /* Do not retry deleting if it fails, as the stream may already
         be in an invalid state. */
      writer->install_stream = NULL;
      SVN_ERR(svn_stream__install_delete(stream, writer->pool));
    }

  return SVN_NO_ERROR;
}
