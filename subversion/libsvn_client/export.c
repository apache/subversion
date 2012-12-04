/*
 * export.c:  export a tree.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_file_io.h>
#include <apr_md5.h>
#include "svn_types.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_subst.h"
#include "svn_time.h"
#include "svn_props.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_delta_private.h"


/*** Code. ***/

/* Add EXTERNALS_PROP_VAL for the export destination path PATH to
   TRAVERSAL_INFO.  */
static svn_error_t *
add_externals(apr_hash_t *externals,
              const char *path,
              const svn_string_t *externals_prop_val)
{
  apr_pool_t *pool = apr_hash_pool_get(externals);
  const char *local_abspath;

  if (! externals_prop_val)
    return SVN_NO_ERROR;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  apr_hash_set(externals, local_abspath, APR_HASH_KEY_STRING,
               apr_pstrmemdup(pool, externals_prop_val->data,
                              externals_prop_val->len));

  return SVN_NO_ERROR;
}

/* Helper function that gets the eol style and optionally overrides the
   EOL marker for files marked as native with the EOL marker matching
   the string specified in requested_value which is of the same format
   as the svn:eol-style property values. */
static svn_error_t *
get_eol_style(svn_subst_eol_style_t *style,
              const char **eol,
              const char *value,
              const char *requested_value)
{
  svn_subst_eol_style_from_value(style, eol, value);
  if (requested_value && *style == svn_subst_eol_style_native)
    {
      svn_subst_eol_style_t requested_style;
      const char *requested_eol;

      svn_subst_eol_style_from_value(&requested_style, &requested_eol,
                                     requested_value);

      if (requested_style == svn_subst_eol_style_fixed)
        *eol = requested_eol;
      else
        return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                 _("'%s' is not a valid EOL value"),
                                 requested_value);
    }
  return SVN_NO_ERROR;
}

/* If *APPENDABLE_DIRENT_P represents an existing directory, then append
 * to it the basename of BASENAME_OF and return the result in
 * *APPENDABLE_DIRENT_P.  The kind of BASENAME_OF is either dirent or uri,
 * as given by IS_URI.
 */
static svn_error_t *
append_basename_if_dir(const char **appendable_dirent_p,
                       const char *basename_of,
                       svn_boolean_t is_uri,
                       apr_pool_t *pool)
{
  svn_node_kind_t local_kind;
  SVN_ERR(svn_io_check_resolved_path(*appendable_dirent_p, &local_kind, pool));
  if (local_kind == svn_node_dir)
    {
      const char *base_name;

      if (is_uri)
        base_name = svn_uri_basename(basename_of, pool);
      else
        base_name = svn_dirent_basename(basename_of, NULL);

      *appendable_dirent_p = svn_dirent_join(*appendable_dirent_p,
                                             base_name, pool);
    }

  return SVN_NO_ERROR;
}

/* Make an unversioned copy of the versioned file at FROM_ABSPATH.  Copy it
 * to the destination path TO_ABSPATH.
 *
 * If REVISION is svn_opt_revision_working, copy the working version,
 * otherwise copy the base version.
 *
 * Expand the file's keywords according to the source file's 'svn:keywords'
 * property, if present.  If copying a locally modified working version,
 * append 'M' to the revision number and use '(local)' for the author.
 *
 * Translate the file's line endings according to the source file's
 * 'svn:eol-style' property, if present.  If NATIVE_EOL is not NULL, use it
 * in place of the native EOL style.  Throw an error if the source file has
 * inconsistent line endings and EOL translation is attempted.
 *
 * Set the destination file's modification time to the source file's
 * modification time if copying the working version and the working version
 * is locally modified; otherwise set it to the versioned file's last
 * changed time.
 *
 * Set the destination file's 'executable' flag according to the source
 * file's 'svn:executable' property.
 */

/* baton for export_node */
struct export_info_baton
{
  const char *to_path;
  const svn_opt_revision_t *revision;
  svn_boolean_t ignore_keywords;
  svn_boolean_t overwrite;
  svn_wc_context_t *wc_ctx;
  const char *native_eol;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  const char *origin_abspath;
  svn_boolean_t exported;
};

/* Export a file or directory. Implements svn_wc_status_func4_t */
static svn_error_t *
export_node(void *baton,
            const char *local_abspath,
            const svn_wc_status3_t *status,
            apr_pool_t *scratch_pool)
{
  struct export_info_baton *eib = baton;
  svn_wc_context_t *wc_ctx = eib->wc_ctx;
  apr_hash_t *kw = NULL;
  svn_subst_eol_style_t style;
  apr_hash_t *props;
  svn_string_t *eol_style, *keywords, *executable, *special;
  const char *eol = NULL;
  svn_boolean_t local_mod = FALSE;
  apr_time_t tm;
  svn_stream_t *source;
  svn_stream_t *dst_stream;
  const char *dst_tmp;
  svn_error_t *err;
  
  const char *to_abspath = svn_dirent_join(
                                eib->to_path,
                                svn_dirent_skip_ancestor(eib->origin_abspath,
                                                         local_abspath),
                                scratch_pool);

  eib->exported = TRUE;

  /* Don't export 'deleted' files and directories unless it's a
     revision other than WORKING.  These files and directories
     don't really exist in WORKING. */
  if (eib->revision->kind == svn_opt_revision_working
      && status->node_status == svn_wc_status_deleted)
    return SVN_NO_ERROR;

  if (status->kind == svn_node_dir)
    {
      apr_fileperms_t perm = APR_OS_DEFAULT;

      /* Try to make the new directory.  If this fails because the
         directory already exists, check our FORCE flag to see if we
         care. */

      /* Keep the source directory's permissions if applicable.
         Skip retrieving the umask on windows. Apr does not implement setting
         filesystem privileges on Windows.
         Retrieving the file permissions with APR_FINFO_PROT | APR_FINFO_OWNER
         is documented to be 'incredibly expensive' */
#ifndef WIN32
      if (eib->revision->kind == svn_opt_revision_working)
        {
          apr_finfo_t finfo;
          SVN_ERR(svn_io_stat(&finfo, local_abspath, APR_FINFO_PROT,
                              scratch_pool));
          perm = finfo.protection;
        }
#endif
      err = svn_io_dir_make(to_abspath, perm, scratch_pool);
      if (err)
        {
          if (! APR_STATUS_IS_EEXIST(err->apr_err))
            return svn_error_trace(err);
          if (! eib->overwrite)
            SVN_ERR_W(err, _("Destination directory exists, and will not be "
                             "overwritten unless forced"));
          else
            svn_error_clear(err);
        }

      if (eib->notify_func
          && (strcmp(eib->origin_abspath, local_abspath) != 0))
        {
          svn_wc_notify_t *notify =
              svn_wc_create_notify(to_abspath,
                                   svn_wc_notify_update_add, scratch_pool);

          notify->kind = svn_node_dir;
          (eib->notify_func)(eib->notify_baton, notify, scratch_pool);
        }

      return SVN_NO_ERROR;
    }
  else if (status->kind != svn_node_file)
    {
      if (strcmp(eib->origin_abspath, local_abspath) != 0)
        return SVN_NO_ERROR;

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  if (status->file_external)
    return SVN_NO_ERROR;

  /* Produce overwrite errors for the export root */
  if (strcmp(local_abspath, eib->origin_abspath) == 0)
    {
      svn_node_kind_t to_kind;

      SVN_ERR(svn_io_check_path(to_abspath, &to_kind, scratch_pool));

      if ((to_kind == svn_node_file || to_kind == svn_node_unknown)
          && !eib->overwrite)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Destination file '%s' exists, and "
                                   "will not be overwritten unless forced"),
                                 svn_dirent_local_style(to_abspath,
                                                        scratch_pool));
      else if (to_kind == svn_node_dir)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Destination '%s' exists. Cannot "
                                   "overwrite directory with non-directory"),
                                 svn_dirent_local_style(to_abspath,
                                                        scratch_pool));
    }

  if (eib->revision->kind != svn_opt_revision_working)
    {
      /* Only export 'added' files when the revision is WORKING. This is not
         WORKING, so skip the 'added' files, since they didn't exist
         in the BASE revision and don't have an associated text-base.

         'replaced' files are technically the same as 'added' files.
         ### TODO: Handle replaced nodes properly.
         ###       svn_opt_revision_base refers to the "new"
         ###       base of the node. That means, if a node is locally
         ###       replaced, export skips this node, as if it was locally
         ###       added, because svn_opt_revision_base refers to the base
         ###       of the added node, not to the node that was deleted.
         ###       In contrast, when the node is copied-here or moved-here,
         ###       the copy/move source's content will be exported.
         ###       It is currently not possible to export the revert-base
         ###       when a node is locally replaced. We need a new
         ###       svn_opt_revision_ enum value for proper distinction
         ###       between revert-base and commit-base.

         Copied-/moved-here nodes have a base, so export both added and
         replaced files when they involve a copy-/move-here.

         We get all this for free from evaluating SOURCE == NULL:
       */
      SVN_ERR(svn_wc_get_pristine_contents2(&source, wc_ctx, local_abspath,
                                            scratch_pool, scratch_pool));
      if (source == NULL)
        return SVN_NO_ERROR;

      SVN_ERR(svn_wc_get_pristine_props(&props, wc_ctx, local_abspath,
                                        scratch_pool, scratch_pool));
    }
  else
    {
      /* ### hmm. this isn't always a specialfile. this will simply open
         ### the file readonly if it is a regular file. */
      SVN_ERR(svn_subst_read_specialfile(&source, local_abspath, scratch_pool,
                                         scratch_pool));

      SVN_ERR(svn_wc_prop_list2(&props, wc_ctx, local_abspath, scratch_pool,
                                scratch_pool));
      if (status->node_status != svn_wc_status_normal)
        local_mod = TRUE;
    }

  /* We can early-exit if we're creating a special file. */
  special = apr_hash_get(props, SVN_PROP_SPECIAL,
                         APR_HASH_KEY_STRING);
  if (special != NULL)
    {
      /* Create the destination as a special file, and copy the source
         details into the destination stream. */
      /* ### And forget the notification */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, to_abspath,
                                           scratch_pool, scratch_pool));
      return svn_error_trace(
        svn_stream_copy3(source, dst_stream, NULL, NULL, scratch_pool));
    }


  eol_style = apr_hash_get(props, SVN_PROP_EOL_STYLE,
                           APR_HASH_KEY_STRING);
  keywords = apr_hash_get(props, SVN_PROP_KEYWORDS,
                          APR_HASH_KEY_STRING);
  executable = apr_hash_get(props, SVN_PROP_EXECUTABLE,
                            APR_HASH_KEY_STRING);

  if (eol_style)
    SVN_ERR(get_eol_style(&style, &eol, eol_style->data, eib->native_eol));

  if (local_mod)
    {
      /* Use the modified time from the working copy of
         the file */
      SVN_ERR(svn_io_file_affected_time(&tm, local_abspath, scratch_pool));
    }
  else
    {
      tm = status->changed_date;
    }

  if (keywords)
    {
      svn_revnum_t changed_rev = status->changed_rev;
      const char *suffix;
      const char *url = svn_path_url_add_component2(status->repos_root_url,
                                                    status->repos_relpath,
                                                    scratch_pool);
      const char *author = status->changed_author;
      if (local_mod)
        {
          /* For locally modified files, we'll append an 'M'
             to the revision number, and set the author to
             "(local)" since we can't always determine the
             current user's username */
          suffix = "M";
          author = _("(local)");
        }
      else
        {
          suffix = "";
        }

      SVN_ERR(svn_subst_build_keywords2
              (&kw, keywords->data,
               apr_psprintf(scratch_pool, "%ld%s", changed_rev, suffix),
               url, tm, author, scratch_pool));
    }

  /* For atomicity, we translate to a tmp file and then rename the tmp file
     over the real destination. */
  SVN_ERR(svn_stream_open_unique(&dst_stream, &dst_tmp,
                                 svn_dirent_dirname(to_abspath, scratch_pool),
                                 svn_io_file_del_none, scratch_pool,
                                 scratch_pool));

  /* If some translation is needed, then wrap the output stream (this is
     more efficient than wrapping the input). */
  if (eol || (kw && (apr_hash_count(kw) > 0)))
    dst_stream = svn_subst_stream_translated(dst_stream,
                                             eol,
                                             FALSE /* repair */,
                                             kw,
                                             ! eib->ignore_keywords /* expand */,
                                             scratch_pool);

  /* ###: use cancel func/baton in place of NULL/NULL below. */
  err = svn_stream_copy3(source, dst_stream, NULL, NULL, scratch_pool);

  if (!err && executable)
    err = svn_io_set_file_executable(dst_tmp, TRUE, FALSE, scratch_pool);

  if (!err)
    err = svn_io_set_file_affected_time(tm, dst_tmp, scratch_pool);

  if (err)
    return svn_error_compose_create(err, svn_io_remove_file2(dst_tmp, FALSE,
                                                             scratch_pool));

  /* Now that dst_tmp contains the translated data, do the atomic rename. */
  SVN_ERR(svn_io_file_rename(dst_tmp, to_abspath, scratch_pool));

  if (eib->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(to_abspath,
                                      svn_wc_notify_update_add, scratch_pool);
      notify->kind = svn_node_file;
      (eib->notify_func)(eib->notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Abstraction of open_root.
 *
 * Create PATH if it does not exist and is not obstructed, and invoke
 * NOTIFY_FUNC with NOTIFY_BATON on PATH.
 *
 * If PATH exists but is a file, then error with SVN_ERR_WC_NOT_WORKING_COPY.
 *
 * If PATH is a already a directory, then error with
 * SVN_ERR_WC_OBSTRUCTED_UPDATE, unless FORCE, in which case just
 * export into PATH with no error.
 */
static svn_error_t *
open_root_internal(const char *path,
                   svn_boolean_t force,
                   svn_wc_notify_func2_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_none)
    SVN_ERR(svn_io_make_dir_recursively(path, pool));
  else if (kind == svn_node_file)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' exists and is not a directory"),
                             svn_dirent_local_style(path, pool));
  else if ((kind != svn_node_dir) || (! force))
    return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                             _("'%s' already exists"),
                             svn_dirent_local_style(path, pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(path,
                                                     svn_wc_notify_update_add,
                                                     pool);
      notify->kind = svn_node_dir;
      (*notify_func)(notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */


/*** A dedicated 'export' editor, which does no .svn/ accounting.  ***/


struct edit_baton
{
  const char *root_path;
  const char *root_url;
  svn_boolean_t force;
  svn_revnum_t *target_revision;
  apr_hash_t *externals;
  const char *native_eol;
  svn_boolean_t ignore_keywords;

  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};


static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  /* Always use empty props, since the node won't have pre-existing props
     (This is an export, remember?) */
  *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  /* An export always gets text against the empty stream (i.e, full texts). */
  *filename = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(void *baton,
         const char *relpath,
         const svn_checksum_t *checksum,
         svn_stream_t *contents,
         apr_hash_t *props,
         svn_revnum_t replaces_rev,
         apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *full_path = svn_dirent_join(eb->root_path, relpath,
                                          scratch_pool);
  /* RELPATH is not canonicalized, i.e. it may still contain spaces etc.
   * but EB->root_url is. */
  const char *full_url = svn_path_url_add_component2(eb->root_url,
                                                     relpath,
                                                     scratch_pool);
  const svn_string_t *val;
  /* The four svn: properties we might actually care about. */
  const svn_string_t *eol_style_val = NULL;
  const svn_string_t *keywords_val = NULL;
  const svn_string_t *executable_val = NULL;
  svn_boolean_t special = FALSE;
  /* Any keyword vals to be substituted */
  const char *revision = NULL;
  const char *author = NULL;
  apr_time_t date = 0; 

  /* Look at any properties for additional information. */
  if ( (val = apr_hash_get(props, SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING)) )
    eol_style_val = val;

  if ( !eb->ignore_keywords && (val = apr_hash_get(props, SVN_PROP_KEYWORDS,
                                                   APR_HASH_KEY_STRING)) )
    keywords_val = val;

  if ( (val = apr_hash_get(props, SVN_PROP_EXECUTABLE, APR_HASH_KEY_STRING)) )
    executable_val = val;
  
  /* Try to fill out the baton's keywords-structure too. */
  if ( (val = apr_hash_get(props, SVN_PROP_ENTRY_COMMITTED_REV,
                           APR_HASH_KEY_STRING)) )
    revision = val->data;

  if ( (val = apr_hash_get(props, SVN_PROP_ENTRY_COMMITTED_DATE,
                           APR_HASH_KEY_STRING)) )
    SVN_ERR(svn_time_from_cstring(&date, val->data, scratch_pool));
  
  if ( (val = apr_hash_get(props, SVN_PROP_ENTRY_LAST_AUTHOR,
                           APR_HASH_KEY_STRING)) )
    author = val->data;

  if ( (val = apr_hash_get(props, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING)) )
    special = TRUE;

  if (special)
    {
      svn_stream_t *tmp_stream;

      SVN_ERR(svn_subst_create_specialfile(&tmp_stream, full_path,
                                           scratch_pool, scratch_pool));
      SVN_ERR(svn_stream_copy3(contents, tmp_stream, eb->cancel_func,
                               eb->cancel_baton, scratch_pool));
    }
  else
    {
      svn_stream_t *tmp_stream;
      const char *tmppath;

      /* Create a temporary file in the same directory as the file. We're going
         to rename the thing into place when we're done. */
      SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmppath,
                                     svn_dirent_dirname(full_path,
                                                        scratch_pool),
                                     svn_io_file_del_none,
                                     scratch_pool, scratch_pool));

      /* Possibly wrap the stream to be translated, as dictated by
         the props. */
      if (eol_style_val || keywords_val)
        {
          svn_subst_eol_style_t style;
          const char *eol = NULL;
          svn_boolean_t repair = FALSE;
          apr_hash_t *final_kw = NULL;

          if (eol_style_val)
            {
              SVN_ERR(get_eol_style(&style, &eol, eol_style_val->data,
                                    eb->native_eol));
              repair = TRUE;
            }

          if (keywords_val)
            SVN_ERR(svn_subst_build_keywords2(&final_kw, keywords_val->data,
                                              revision, full_url, date,
                                              author, scratch_pool));

          /* Writing through a translated stream is more efficient than
             reading through one, so we wrap TMP_STREAM and not CONTENTS. */
          tmp_stream = svn_subst_stream_translated(tmp_stream, eol, repair,
                                                   final_kw, TRUE, /* expand */
                                                   scratch_pool);
        }

      SVN_ERR(svn_stream_copy3(contents, tmp_stream, eb->cancel_func,
                               eb->cancel_baton, scratch_pool));

      /* Move the file into place. */
      SVN_ERR(svn_io_file_rename(tmppath, full_path, scratch_pool));
    }

  if (executable_val)
    SVN_ERR(svn_io_set_file_executable(full_path, TRUE, FALSE, scratch_pool));

  if (date && (! special))
    SVN_ERR(svn_io_set_file_affected_time(date, full_path, scratch_pool));

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(full_path,
                                                     svn_wc_notify_update_add,
                                                     scratch_pool);
      notify->kind = svn_node_file;
      (*eb->notify_func)(eb->notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(void *baton,
              const char *relpath,
              const apr_array_header_t *children,
              apr_hash_t *props,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_node_kind_t kind;
  const char *full_path = svn_dirent_join(eb->root_path, relpath,
                                          scratch_pool);
  svn_string_t *val;

  SVN_ERR(svn_io_check_path(full_path, &kind, scratch_pool));
  if (kind == svn_node_none)
    SVN_ERR(svn_io_dir_make(full_path, APR_OS_DEFAULT, scratch_pool));
  else if (kind == svn_node_file)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' exists and is not a directory"),
                             svn_dirent_local_style(full_path, scratch_pool));
  else if (! (kind == svn_node_dir && eb->force))
    return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                             _("'%s' already exists"),
                             svn_dirent_local_style(full_path, scratch_pool));

  if ( (val = apr_hash_get(props, SVN_PROP_EXTERNALS, APR_HASH_KEY_STRING)) )
    SVN_ERR(add_externals(eb->externals, full_path, val));
  
  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(full_path,
                                                     svn_wc_notify_update_add,
                                                     scratch_pool);
      notify->kind = svn_node_dir;
      (*eb->notify_func)(eb->notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
target_revision_func(void *baton,
                     svn_revnum_t target_revision,
                     apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  *eb->target_revision = target_revision;

  return SVN_NO_ERROR;
}

static svn_error_t *
get_editor(const svn_delta_editor_t **export_editor,
           void **edit_baton,
           struct edit_baton *eb,
           svn_client_ctx_t *ctx,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_editor_t *editor;
  struct svn_delta__extra_baton *exb = apr_pcalloc(result_pool, sizeof(*exb));
  svn_boolean_t *found_abs_paths = apr_palloc(result_pool,
                                              sizeof(*found_abs_paths));

  exb->baton = eb;
  exb->target_revision = target_revision_func;

  SVN_ERR(svn_editor_create(&editor, eb, ctx->cancel_func, ctx->cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_add_directory(editor, add_directory,
                                         scratch_pool));
  SVN_ERR(svn_editor_setcb_add_file(editor, add_file, scratch_pool));

  *found_abs_paths = TRUE;

  SVN_ERR(svn_delta__delta_from_editor(export_editor, edit_baton,
                                       editor, NULL, NULL, found_abs_paths,
                                       NULL, NULL,
                                       fetch_props_func, eb,
                                       fetch_base_func, eb,
                                       exb, result_pool));

  /* Create the root of the export. */
  SVN_ERR(open_root_internal(eb->root_path, eb->force, eb->notify_func,
                             eb->notify_baton, scratch_pool));

  return SVN_NO_ERROR;
}


/*** Public Interfaces ***/

svn_error_t *
svn_client_export5(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t ignore_keywords,
                   svn_depth_t depth,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_revnum_t edit_revision = SVN_INVALID_REVNUM;
  svn_boolean_t from_is_url = svn_path_is_url(from_path_or_url);

  SVN_ERR_ASSERT(peg_revision != NULL);
  SVN_ERR_ASSERT(revision != NULL);

  if (svn_path_is_url(to_path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), to_path);

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                        from_path_or_url);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  if (from_is_url || ! SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      svn_client__pathrev_t *loc;
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;
      struct edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));

      /* Get the RA connection. */
      SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                                from_path_or_url, NULL,
                                                peg_revision,
                                                revision, ctx, pool));

      eb->root_path = to_path;
      eb->root_url = loc->url;
      eb->force = overwrite;
      eb->target_revision = &edit_revision;
      eb->externals = apr_hash_make(pool);
      eb->native_eol = native_eol;
      eb->ignore_keywords = ignore_keywords;
      eb->cancel_func = ctx->cancel_func;
      eb->cancel_baton = ctx->cancel_baton;
      eb->notify_func = ctx->notify_func2;
      eb->notify_baton = ctx->notify_baton2;

      SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind, pool));

      if (kind == svn_node_file)
        {
          apr_hash_t *props;
          svn_stream_t *tmp_stream;
          svn_node_kind_t to_kind;

          if (svn_path_is_empty(to_path))
            {
              if (from_is_url)
                to_path = svn_uri_basename(from_path_or_url, pool);
              else
                to_path = svn_dirent_basename(from_path_or_url, NULL);
              eb->root_path = to_path;
            }
          else
            {
              SVN_ERR(append_basename_if_dir(&to_path, from_path_or_url,
                                             from_is_url, pool));
              eb->root_path = to_path;
            }

          SVN_ERR(svn_io_check_path(to_path, &to_kind, pool));

          if ((to_kind == svn_node_file || to_kind == svn_node_unknown) &&
              ! overwrite)
            return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                     _("Destination file '%s' exists, and "
                                       "will not be overwritten unless forced"),
                                     svn_dirent_local_style(to_path, pool));
          else if (to_kind == svn_node_dir)
            return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                     _("Destination '%s' exists. Cannot "
                                       "overwrite directory with non-directory"),
                                     svn_dirent_local_style(to_path, pool));

          tmp_stream = svn_stream_buffered(pool);

          SVN_ERR(svn_ra_get_file(ra_session, "", loc->rev,
                                  tmp_stream, NULL, &props, pool));

          /* Since you cannot actually root an editor at a file, we
           * manually drive a function of our editor. */
          SVN_ERR(add_file(eb, "", NULL, tmp_stream, props,
                           SVN_INVALID_REVNUM, pool));
        }
      else if (kind == svn_node_dir)
        {
          void *edit_baton;
          const svn_delta_editor_t *export_editor;
          const svn_ra_reporter3_t *reporter;
          void *report_baton;
          svn_boolean_t use_sleep = FALSE;

          SVN_ERR(get_editor(&export_editor, &edit_baton, eb, ctx,
          pool, pool));

          /* Manufacture a basic 'report' to the update reporter. */
          SVN_ERR(svn_ra_do_update2(ra_session,
                                    &reporter, &report_baton,
                                    loc->rev,
                                    "", /* no sub-target */
                                    depth,
                                    FALSE, /* don't want copyfrom-args */
                                    export_editor, edit_baton, pool));

          SVN_ERR(reporter->set_path(report_baton, "", loc->rev,
                                     /* Depth is irrelevant, as we're
                                        passing start_empty=TRUE anyway. */
                                     svn_depth_infinity,
                                     TRUE, /* "help, my dir is empty!" */
                                     NULL, pool));

          SVN_ERR(reporter->finish_report(report_baton, pool));

          /* Special case: Due to our sly export/checkout method of
           * updating an empty directory, no target will have been created
           * if the exported item is itself an empty directory
           * (export_editor->open_root never gets called, because there
           * are no "changes" to make to the empty dir we reported to the
           * repository).
           *
           * So we just create the empty dir manually; but we do it via
           * open_root_internal(), in order to get proper notification.
           */
          SVN_ERR(svn_io_check_path(to_path, &kind, pool));
          if (kind == svn_node_none)
            SVN_ERR(open_root_internal
                    (to_path, overwrite, ctx->notify_func2,
                     ctx->notify_baton2, pool));

          if (! ignore_externals && depth == svn_depth_infinity)
            {
              const char *repos_root_url;
              const char *to_abspath;

              SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url, pool));
              SVN_ERR(svn_dirent_get_absolute(&to_abspath, to_path, pool));
              SVN_ERR(svn_client__export_externals(eb->externals,
                                                   from_path_or_url,
                                                   to_abspath, repos_root_url,
                                                   depth, native_eol,
                                                   ignore_keywords, &use_sleep,
                                                   ctx, pool));
            }
        }
      else if (kind == svn_node_none)
        {
          return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                   _("URL '%s' doesn't exist"),
                                   from_path_or_url);
        }
      /* kind == svn_node_unknown not handled */
    }
  else
    {
      struct export_info_baton eib;
      svn_node_kind_t kind;
      apr_hash_t *externals = NULL;

      /* This is a working copy export. */
      /* just copy the contents of the working copy into the target path. */
      SVN_ERR(svn_dirent_get_absolute(&from_path_or_url, from_path_or_url,
                                      pool));

      SVN_ERR(svn_dirent_get_absolute(&to_path, to_path, pool));

      SVN_ERR(svn_io_check_path(from_path_or_url, &kind, pool));

      /* ### [JAF] If something already exists on disk at the destination path,
       * the behaviour depends on the node kinds of the source and destination
       * and on the FORCE flag.  The intention (I guess) is to follow the
       * semantics of svn_client_export5(), semantics that are not fully
       * documented but would be something like:
       *
       * -----------+---------------------------------------------------------
       *        Src | DIR                 FILE                SPECIAL
       * Dst (disk) +---------------------------------------------------------
       * NONE       | simple copy         simple copy         (as src=file?)
       * DIR        | merge if forced [2] inside if root [1]  (as src=file?)
       * FILE       | err                 overwr if forced[3] (as src=file?)
       * SPECIAL    | ???                 ???                 ???
       * -----------+---------------------------------------------------------
       *
       * [1] FILE onto DIR case: If this file is the root of the copy and thus
       *     the only node to be copied, then copy it as a child of the
       *     directory TO, applying these same rules again except that if this
       *     case occurs again (the child path is already a directory) then
       *     error out.  If this file is not the root of the copy (it is
       *     reached by recursion), then error out.
       *
       * [2] DIR onto DIR case.  If the 'FORCE' flag is true then copy the
       *     source's children inside the target dir, else error out.  When
       *     copying the children, apply the same set of rules, except in the
       *     FILE onto DIR case error out like in note [1].
       *
       * [3] If the 'FORCE' flag is true then overwrite the destination file
       *     else error out.
       *
       * The reality (apparently, looking at the code) is somewhat different.
       * For a start, to detect the source kind, it looks at what is on disk
       * rather than the versioned working or base node.
       */
      if (kind == svn_node_file)
        SVN_ERR(append_basename_if_dir(&to_path, from_path_or_url, FALSE,
                                       pool));

      eib.to_path = to_path;
      eib.revision = revision;
      eib.overwrite = overwrite;
      eib.ignore_keywords = ignore_keywords;
      eib.wc_ctx = ctx->wc_ctx;
      eib.native_eol = native_eol;
      eib.notify_func = ctx->notify_func2;;
      eib.notify_baton = ctx->notify_baton2;
      eib.origin_abspath = from_path_or_url;
      eib.exported = FALSE;

      SVN_ERR(svn_wc_walk_status(ctx->wc_ctx, from_path_or_url, depth,
                                 TRUE /* get_all */,
                                 TRUE /* no_ignore */,
                                 FALSE /* ignore_text_mods */,
                                 NULL,
                                 export_node, &eib,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

      if (!eib.exported)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 svn_dirent_local_style(from_path_or_url,
                                                        pool));

      if (!ignore_externals)
        SVN_ERR(svn_wc__externals_defined_below(&externals, ctx->wc_ctx,
                                                from_path_or_url,
                                                pool, pool));

      if (externals && apr_hash_count(externals))
        {
          apr_pool_t *iterpool = svn_pool_create(pool);
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(pool, externals);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *external_abspath = svn__apr_hash_index_key(hi);
              const char *relpath;
              const char *target_abspath;

              svn_pool_clear(iterpool);

              relpath = svn_dirent_skip_ancestor(from_path_or_url,
                                                 external_abspath);

              target_abspath = svn_dirent_join(to_path, relpath,
                                                         iterpool);

              /* Ensure that the parent directory exists */
              SVN_ERR(svn_io_make_dir_recursively(
                            svn_dirent_dirname(target_abspath, iterpool),
                            iterpool));

              SVN_ERR(svn_client_export5(NULL,
                                         svn_dirent_join(from_path_or_url,
                                                         relpath,
                                                         iterpool),
                                         target_abspath,
                                         peg_revision, revision,
                                         TRUE, ignore_externals,
                                         ignore_keywords, depth, native_eol,
                                         ctx, iterpool));
            }

          svn_pool_destroy(iterpool);
        }
    }


  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(to_path,
                               svn_wc_notify_update_completed, pool);
      notify->revision = edit_revision;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  if (result_rev)
    *result_rev = edit_revision;

  return SVN_NO_ERROR;
}
