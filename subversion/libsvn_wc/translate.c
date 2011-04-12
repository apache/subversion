/*
 * translate.c :  wc-specific eol/keyword substitution
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



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_props.h"

#include "wc.h"
#include "adm_files.h"
#include "translate.h"
#include "props.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



/* */
static svn_error_t *
read_handler_unsupported(void *baton, char *buffer, apr_size_t *len)
{
  SVN_ERR_MALFUNCTION();
}

/* */
static svn_error_t *
write_handler_unsupported(void *baton, const char *buffer, apr_size_t *len)
{
  SVN_ERR_MALFUNCTION();
}

svn_error_t *
svn_wc__internal_translated_stream(svn_stream_t **stream,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *versioned_abspath,
                                   apr_uint32_t flags,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_boolean_t special;
  svn_boolean_t to_nf = flags & SVN_WC_TRANSLATE_TO_NF;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t repair_forced = flags & SVN_WC_TRANSLATE_FORCE_EOL_REPAIR;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_abspath));

  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, versioned_abspath, NULL,
                                     scratch_pool, scratch_pool));

  if (special)
    {
      if (to_nf)
        return svn_subst_read_specialfile(stream, local_abspath, result_pool,
                                          scratch_pool);

      return svn_subst_create_specialfile(stream, local_abspath, result_pool,
                                          scratch_pool);
    }

  if (to_nf)
    SVN_ERR(svn_stream_open_readonly(stream, local_abspath, result_pool,
                                     scratch_pool));
  else
    {
      apr_file_t *file;

      /* We don't want the "open-exclusively" feature of the normal
         svn_stream_open_writable interface. Do this manually. */
      SVN_ERR(svn_io_file_open(&file, local_abspath,
                               APR_CREATE | APR_WRITE | APR_BUFFERED,
                               APR_OS_DEFAULT, result_pool));
      *stream = svn_stream_from_aprfile2(file, FALSE, result_pool);
    }

  if (svn_subst_translation_required(style, eol, keywords, special, TRUE))
    {
      if (to_nf)
        {
          if (style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;
          else if (style == svn_subst_eol_style_fixed)
            repair_forced = TRUE;
          else if (style != svn_subst_eol_style_none)
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

          /* Wrap the stream to translate to normal form */
          *stream = svn_subst_stream_translated(*stream,
                                                eol,
                                                repair_forced,
                                                keywords,
                                                FALSE /* expand */,
                                                result_pool);

          /* Enforce our contract. TO_NF streams are readonly */
          svn_stream_set_write(*stream, write_handler_unsupported);
        }
      else
        {
          *stream = svn_subst_stream_translated(*stream, eol, TRUE,
                                                keywords, TRUE, result_pool);

          /* Enforce our contract. FROM_NF streams are write-only */
          svn_stream_set_read(*stream, read_handler_unsupported);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__internal_translated_file(const char **xlated_abspath,
                                 const char *src,
                                 svn_wc__db_t *db,
                                 const char *versioned_abspath,
                                 apr_uint32_t flags,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_subst_eol_style_t style;
  const char *eol;
  const char *xlated_path;
  apr_hash_t *keywords;
  svn_boolean_t special;


  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_abspath));
  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, versioned_abspath, NULL, 
                                     scratch_pool, scratch_pool));

  if (! svn_subst_translation_required(style, eol, keywords, special, TRUE)
      && (! (flags & SVN_WC_TRANSLATE_FORCE_COPY)))
    {
      /* Translation would be a no-op, so return the original file. */
      xlated_path = src;
    }
  else  /* some translation (or copying) is necessary */
    {
      const char *tmp_dir;
      const char *tmp_vfile;
      svn_boolean_t repair_forced
          = (flags & SVN_WC_TRANSLATE_FORCE_EOL_REPAIR) != 0;
      svn_boolean_t expand = (flags & SVN_WC_TRANSLATE_TO_NF) == 0;

      if (flags & SVN_WC_TRANSLATE_USE_GLOBAL_TMP)
        tmp_dir = NULL;
      else
        SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmp_dir, db, versioned_abspath,
                                               scratch_pool, scratch_pool));

      SVN_ERR(svn_io_open_unique_file3(NULL, &tmp_vfile, tmp_dir,
                (flags & SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP)
                  ? svn_io_file_del_none
                  : svn_io_file_del_on_pool_cleanup,
                result_pool, scratch_pool));

      /* ### ugh. the repair behavior does NOT match the docstring. bleah.
         ### all of these translation functions are crap and should go
         ### away anyways. we'll just deprecate most of the functions and
         ### properly document the survivors */

      if (expand)
        {
          /* from normal form */

          repair_forced = TRUE;
        }
      else
        {
          /* to normal form */

          if (style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;
          else if (style == svn_subst_eol_style_fixed)
            repair_forced = TRUE;
          else if (style != svn_subst_eol_style_none)
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);
        }

      SVN_ERR(svn_subst_copy_and_translate4(src, tmp_vfile,
                                            eol, repair_forced,
                                            keywords,
                                            expand,
                                            special,
                                            cancel_func, cancel_baton,
                                            result_pool));

      xlated_path = tmp_vfile;
    }
  SVN_ERR(svn_dirent_get_absolute(xlated_abspath, xlated_path, result_pool));

  return SVN_NO_ERROR;
}

void
svn_wc__eol_value_from_string(const char **value, const char *eol)
{
  if (eol == NULL)
    *value = NULL;
  else if (! strcmp("\n", eol))
    *value = "LF";
  else if (! strcmp("\r", eol))
    *value = "CR";
  else if (! strcmp("\r\n", eol))
    *value = "CRLF";
  else
    *value = NULL;
}

svn_error_t *
svn_wc__get_translate_info(svn_subst_eol_style_t *style,
                           const char **eol,
                           apr_hash_t **keywords,
                           svn_boolean_t *special,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_hash_t *props,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_string_t *propval;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (props == NULL)
    SVN_ERR(svn_wc__get_actual_props(&props, db, local_abspath,
                                     scratch_pool, scratch_pool));

  if (eol)
    {
      propval = props ? apr_hash_get(props, SVN_PROP_EOL_STYLE,
                                     APR_HASH_KEY_STRING) : NULL;

      svn_subst_eol_style_from_value(style, eol, propval ? propval->data : NULL);
    }

  if (keywords)
    {
      propval = props ? apr_hash_get(props, SVN_PROP_KEYWORDS,
                                     APR_HASH_KEY_STRING) : NULL;

      if (!propval || propval->len == 0)
        *keywords = NULL;
      else
        SVN_ERR(svn_wc__expand_keywords(keywords,
                                        db, local_abspath,
                                        propval->data,
                                        result_pool, scratch_pool));
    }
  if (special)
    {
      propval = props ? apr_hash_get(props, SVN_PROP_SPECIAL,
                                     APR_HASH_KEY_STRING) : NULL;

      *special = (propval != NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__expand_keywords(apr_hash_t **keywords,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const char *keyword_list,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const char *url;

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL,
                               NULL, NULL, &changed_rev,
                               &changed_date, &changed_author, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_read_url(&url, db, local_abspath, scratch_pool,
                              scratch_pool));

  SVN_ERR(svn_subst_build_keywords2(keywords,
                                    keyword_list,
                                    apr_psprintf(scratch_pool, "%ld",
                                                 changed_rev),
                                    url,
                                    changed_date,
                                    changed_author,
                                    result_pool));

  if (apr_hash_count(*keywords) == 0)
    *keywords = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__maybe_set_executable(svn_boolean_t *did_set,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool)
{
#ifndef WIN32
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  apr_hash_t *props;

  if (did_set)
    *did_set = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_node_install_info(NULL, &status, &kind, NULL, NULL,
                                            &props,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));

  if (kind != svn_wc__db_kind_file
      || status != svn_wc__db_status_normal
      || props == NULL
      || ! apr_hash_get(props, SVN_PROP_EXECUTABLE, APR_HASH_KEY_STRING))
    return SVN_NO_ERROR; /* Not executable */

  SVN_ERR(svn_io_set_file_executable(local_abspath, TRUE, FALSE,
                                     scratch_pool));
  if (did_set)
    *did_set = TRUE;
#else
  if (did_set)
    *did_set = FALSE;
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__maybe_set_read_only(svn_boolean_t *did_set,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_wc__db_lock_t *lock;
  apr_hash_t *props;

  if (did_set)
    *did_set = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_node_install_info(NULL, &status, &kind, NULL, NULL,
                                            &props,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));

  if (kind != svn_wc__db_kind_file
      || status != svn_wc__db_status_normal
      || props == NULL
      || ! apr_hash_get(props, SVN_PROP_NEEDS_LOCK, APR_HASH_KEY_STRING))
    return SVN_NO_ERROR; /* Doesn't need lock handling */

  SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   &lock, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (lock)
    return SVN_NO_ERROR; /* We have a lock */

  SVN_ERR(svn_io_set_file_read_only(local_abspath, FALSE, scratch_pool));
  if (did_set)
    *did_set = TRUE;

  return SVN_NO_ERROR;
}
