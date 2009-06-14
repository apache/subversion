/*
 * translate.c :  wc-specific eol/keyword substitution
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
#include "lock.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



static svn_error_t *
read_handler_unsupported(void *baton, char *buffer, apr_size_t *len)
{
  SVN_ERR_MALFUNCTION();
}

static svn_error_t *
write_handler_unsupported(void *baton, const char *buffer, apr_size_t *len)
{
  SVN_ERR_MALFUNCTION();
}

svn_error_t *
svn_wc_translated_stream(svn_stream_t **stream,
                         const char *path,
                         const char *versioned_file,
                         svn_wc_adm_access_t *adm_access,
                         apr_uint32_t flags,
                         apr_pool_t *pool)
{
  svn_boolean_t special;
  svn_boolean_t to_nf = flags & SVN_WC_TRANSLATE_TO_NF;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t repair_forced = flags & SVN_WC_TRANSLATE_FORCE_EOL_REPAIR;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *versioned_abspath;

  SVN_ERR(svn_dirent_get_absolute(&versioned_abspath, versioned_file, pool));
  SVN_ERR(svn_wc__get_special(&special, db, versioned_abspath, pool));

  if (special)
    {
      if (to_nf)
        return svn_subst_read_specialfile(stream, path, pool, pool);

      return svn_subst_create_specialfile(stream, path, pool, pool);
    }

  SVN_ERR(svn_wc__get_eol_style(&style, &eol, db, versioned_abspath,
                                pool, pool));
  SVN_ERR(svn_wc__get_keywords(&keywords, db, versioned_abspath, NULL, pool,
                               pool));

  if (to_nf)
    SVN_ERR(svn_stream_open_readonly(stream, path, pool, pool));
  else
    {
      apr_file_t *file;

      /* We don't want the "open-exclusively" feature of the normal
         svn_stream_open_writable interface. Do this manually. */
      SVN_ERR(svn_io_file_open(&file, path,
                               APR_CREATE | APR_WRITE | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      *stream = svn_stream_from_aprfile2(file, FALSE, pool);
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
                                                pool);

          /* Enforce our contract. TO_NF streams are readonly */
          svn_stream_set_write(*stream, write_handler_unsupported);
        }
      else
        {
          *stream = svn_subst_stream_translated(*stream, eol, TRUE,
                                                keywords, TRUE, pool);

          /* Enforce our contract. FROM_NF streams are write-only */
          svn_stream_set_read(*stream, read_handler_unsupported);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_translated_file2(const char **xlated_path,
                        const char *src,
                        const char *versioned_file,
                        svn_wc_adm_access_t *adm_access,
                        apr_uint32_t flags,
                        apr_pool_t *pool)
{
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *versioned_abspath;

  SVN_ERR(svn_dirent_get_absolute(&versioned_abspath, versioned_file, pool));
  SVN_ERR(svn_wc__get_eol_style(&style, &eol, db, versioned_abspath,
                                pool, pool));
  SVN_ERR(svn_wc__get_keywords(&keywords, db, versioned_abspath, NULL, pool,
                               pool));
  SVN_ERR(svn_wc__get_special(&special, db, versioned_abspath, pool));

  if (! svn_subst_translation_required(style, eol, keywords, special, TRUE)
      && (! (flags & SVN_WC_TRANSLATE_FORCE_COPY)))
    {
      /* Translation would be a no-op, so return the original file. */
      *xlated_path = src;
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
        tmp_dir = svn_wc__adm_child(svn_dirent_dirname(versioned_file, pool),
                                    SVN_WC__ADM_TMP, pool);

      SVN_ERR(svn_io_open_unique_file3(NULL, &tmp_vfile, tmp_dir,
                (flags & SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP)
                  ? svn_io_file_del_none
                  : svn_io_file_del_on_pool_cleanup,
                pool, pool));

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

      SVN_ERR(svn_subst_copy_and_translate3(src, tmp_vfile,
                                            eol, repair_forced,
                                            keywords,
                                            expand,
                                            special,
                                            pool));

      *xlated_path = tmp_vfile;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_eol_style(svn_subst_eol_style_t *style,
                      const char **eol,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  const svn_string_t *propval;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Get the property value. */
  SVN_ERR(svn_wc__internal_propget(&propval, SVN_PROP_EOL_STYLE, local_abspath,
                                   db, result_pool, scratch_pool));

  /* Convert it. */
  svn_subst_eol_style_from_value(style, eol, propval ? propval->data : NULL);

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
svn_wc__get_keywords(apr_hash_t **keywords,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const char *force_list,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  const char *list;
  const char *repos_relpath;
  const char *repos_root_url;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const char *url;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Choose a property list to parse:  either the one that came into
     this function, or the one attached to PATH. */
  if (force_list == NULL)
    {
      const svn_string_t *propval;

      SVN_ERR(svn_wc__internal_propget(&propval, SVN_PROP_KEYWORDS,
                                       local_abspath, db, scratch_pool,
                                       scratch_pool));

      /* The easy answer. */
      if (propval == NULL)
        {
          *keywords = NULL;
          return SVN_NO_ERROR;
        }

      list = propval->data;
    }
  else
    list = force_list;

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, &repos_relpath,
                               &repos_root_url, NULL, &changed_rev,
                               &changed_date, &changed_author, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  if (repos_root_url == NULL)
    {
      SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, &repos_relpath,
                                       &repos_root_url,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, local_abspath, scratch_pool,
                                       scratch_pool));
    }
  url = svn_uri_join(repos_root_url, repos_relpath, scratch_pool);

  SVN_ERR(svn_subst_build_keywords2(keywords,
                                    list,
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
svn_wc__get_special(svn_boolean_t *special,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  const svn_string_t *propval;

  /* Get the property value. */
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(svn_wc__internal_propget(&propval, SVN_PROP_SPECIAL, local_abspath,
                                   db, scratch_pool, scratch_pool));
  *special = propval != NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__maybe_set_executable(svn_boolean_t *did_set,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool)
{
  const svn_string_t *propval;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__internal_propget(&propval, SVN_PROP_EXECUTABLE,
                                   local_abspath, db, scratch_pool,
                                   scratch_pool));
  if (propval != NULL)
    {
      SVN_ERR(svn_io_set_file_executable(local_abspath, TRUE, FALSE,
                                         scratch_pool));
      if (did_set)
        *did_set = TRUE;
    }
  else if (did_set)
    *did_set = FALSE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__maybe_set_read_only(svn_boolean_t *did_set,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  const svn_string_t *needs_lock;
  svn_wc__db_lock_t *lock;
  svn_error_t *err;

  if (did_set)
    *did_set = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  err = svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, &lock,
                             db, local_abspath, scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      /* If the path wasn't versioned, we still want to set it to read-only. */
      svn_error_clear(err);
    }
  else if (err)
    return svn_error_return(err);
  else if (lock)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__internal_propget(&needs_lock, SVN_PROP_NEEDS_LOCK,
                                   local_abspath, db, scratch_pool,
                                   scratch_pool));
  if (needs_lock != NULL)
    {
      SVN_ERR(svn_io_set_file_read_only(local_abspath, FALSE, scratch_pool));
      if (did_set)
        *did_set = TRUE;
    }

  return SVN_NO_ERROR;
}
