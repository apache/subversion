/*
 * patch.c: patch application support
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

#include <apr_hash.h>
#include <apr_fnmatch.h>
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_diff.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "svn_wc.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_eol_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_dep_compat.h"

typedef struct hunk_info_t {
  /* The hunk. */
  const svn_diff_hunk_t *hunk;

  /* The line where the hunk matched in the target file. */
  svn_linenum_t matched_line;

  /* Whether this hunk has been rejected. */
  svn_boolean_t rejected;

  /* Whether this hunk has already been applied (either manually
   * or by an earlier run of patch). */
  svn_boolean_t already_applied;

  /* The fuzz factor used when matching this hunk, i.e. how many
   * lines of leading and trailing context to ignore during matching. */
  int fuzz;
} hunk_info_t;

/* A struct carrying the information related to the content of a target, be it
 * a property or the the text of a file. */
typedef struct target_content_info_t {
  /* A stream to read lines form the target file. This is NULL in case
   * the target file did not exist prior to patch application. */
  svn_stream_t *stream;

  /* The patched stream, write-only, not seekable.
   * This stream is equivalent to the target, except that in appropriate
   * places it contains the modified text as it appears in the patch file.
   * The data in the underlying file needs to be in repository-normal form,
   * so EOL transformation and keyword contraction is done transparently. */
  svn_stream_t *patched;

  /* The reject stream, write-only, not seekable.
   * Hunks that are rejected will be written to this stream. */
  svn_stream_t *reject;

  /* The line last read from the target file. */
  svn_linenum_t current_line;

  /* The EOL-style of the target. Either 'none', 'fixed', or 'native'.
   * See the documentation of svn_subst_eol_style_t. */
  svn_subst_eol_style_t eol_style;

  /* If the EOL_STYLE above is not 'none', this is the EOL string
   * corresponding to the EOL-style. Else, it is the EOL string the
   * last line read from the target file was using. */
  const char *eol_str;

  /* An array containing stream markers marking the beginning
   * each line in the target stream. */
  apr_array_header_t *lines;

  /* An array containing hunk_info_t structures for hunks already matched. */
  apr_array_header_t *hunks;

  /* True if end-of-file was reached while reading from the target. */
  svn_boolean_t eof;

  /* The keywords of the target. */
  apr_hash_t *keywords;

  /* The pool the target_info is allocated in. */
  apr_pool_t *pool;
} target_content_info_t;

typedef struct prop_patch_target_t {

  /* The name of the property */
  const char *name;

  /* All the information that is specific to the content of the property. */
  target_content_info_t *content_info;

  /* Path to the temporary file underlying the result stream. */
  const char *patched_path;

  /* Represents the operation performed on the property. It can be added,
   * deleted or modified. 
   * ### Should we use flags instead since we're not using all enum values? */
  svn_diff_operation_kind_t operation;

  /* ### Here we'll add flags telling if the prop was added, deleted,
   * ### had_rejects, had_local_mods prior to patching and so on. */
} prop_patch_target_t;

typedef struct patch_target_t {
  /* The target path as it appeared in the patch file,
   * but in canonicalised form. */
  const char *canon_path_from_patchfile;

  /* The target path, relative to the working copy directory the
   * patch is being applied to. A patch strip count applies to this
   * and only this path. This is never NULL. */
  const char *local_relpath;

  /* The absolute path of the target on the filesystem.
   * Any symlinks the path from the patch file may contain are resolved.
   * Is not always known, so it may be NULL. */
  const char *local_abspath;

  /* The target file, read-only, seekable. This is NULL in case the target
   * file did not exist prior to patch application. */
  apr_file_t *file;

  /* Path to the temporary file underlying the result stream. */
  const char *patched_path;

  /* Path to the temporary file underlying the reject stream. */
  const char *reject_path;

  /* The node kind of the target as found in WC-DB prior
   * to patch application. */
  svn_node_kind_t db_kind;

  /* The target's kind on disk prior to patch application. */
  svn_node_kind_t kind_on_disk;

  /* True if the target was locally deleted prior to patching. */
  svn_boolean_t locally_deleted;

  /* True if the target had to be skipped for some reason. */
  svn_boolean_t skipped;

  /* True if the target has been filtered by the patch callback. */
  svn_boolean_t filtered;

  /* True if at least one hunk was rejected. */
  svn_boolean_t had_rejects;

  /* True if at least one property hunk was rejected. */
  svn_boolean_t had_prop_rejects;

  /* True if the target file had local modifications before the
   * patch was applied to it. */
  svn_boolean_t local_mods;

  /* True if the target was added by the patch, which means that it did
   * not exist on disk before patching and has content after patching. */
  svn_boolean_t added;

  /* True if the target ended up being deleted by the patch. */
  svn_boolean_t deleted;

  /* True if the target ended up being replaced by the patch
   * (i.e. a new file was added on top locally deleted node). */
  svn_boolean_t replaced;

  /* True if the target has the executable bit set. */
  svn_boolean_t executable;

  /* True if the patch changed the text of the target. */
  svn_boolean_t has_text_changes;

  /* True if the patch changed any of the properties of the target. */
  svn_boolean_t has_prop_changes;

  /* All the information that is specific to the content of the target. */
  target_content_info_t *content_info;

  /* A hash table of prop_patch_target_t objects keyed by property names. */
  apr_hash_t *prop_targets;

  /* The pool the target is allocated in. */
  apr_pool_t *pool;
} patch_target_t;


/* A smaller struct containing a subset of patch_target_t.
 * Carries the minimal amount of information we still need for a
 * target after we're done patching it so we can free other resources. */
typedef struct patch_target_info_t {
  const char *local_abspath;
  svn_boolean_t deleted;
} patch_target_info_t;


/* Strip STRIP_COUNT components from the front of PATH, returning
 * the result in *RESULT, allocated in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
strip_path(const char **result, const char *path, int strip_count,
           apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *components;
  apr_array_header_t *stripped;

  components = svn_path_decompose(path, scratch_pool);
  if (strip_count >= components->nelts)
    return svn_error_createf(SVN_ERR_CLIENT_PATCH_BAD_STRIP_COUNT, NULL,
                             _("Cannot strip %u components from '%s'"),
                             strip_count,
                             svn_dirent_local_style(path, scratch_pool));

  stripped = apr_array_make(scratch_pool, components->nelts - strip_count,
                            sizeof(const char *));
  for (i = strip_count; i < components->nelts; i++)
    {
      const char *component;

      component = APR_ARRAY_IDX(components, i, const char *);
      APR_ARRAY_PUSH(stripped, const char *) = component;
    }

  *result = svn_path_compose(stripped, result_pool);

  return SVN_NO_ERROR;
}

/* Obtain KEYWORDS, EOL_STYLE and EOL_STR for LOCAL_ABSPATH.
 * WC_CTX is a context for the working copy the patch is applied to.
 * Use RESULT_POOL for allocations of fields in TARGET.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
obtain_eol_and_keywords_for_file(apr_hash_t **keywords,
                                 svn_subst_eol_style_t *eol_style,
                                 const char **eol_str,
                                 svn_wc_context_t *wc_ctx,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  svn_string_t *keywords_val, *eol_style_val;

  SVN_ERR(svn_wc_prop_list2(&props, wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));
  keywords_val = apr_hash_get(props, SVN_PROP_KEYWORDS,
                              APR_HASH_KEY_STRING);
  if (keywords_val)
    {
      svn_revnum_t changed_rev;
      apr_time_t changed_date;
      const char *rev_str;
      const char *author;
      const char *url;

      SVN_ERR(svn_wc__node_get_changed_info(&changed_rev,
                                            &changed_date,
                                            &author, wc_ctx,
                                            local_abspath,
                                            scratch_pool,
                                            scratch_pool));
      rev_str = apr_psprintf(scratch_pool, "%ld", changed_rev);
      SVN_ERR(svn_wc__node_get_url(&url, wc_ctx,
                                   local_abspath,
                                   scratch_pool, scratch_pool));
      SVN_ERR(svn_subst_build_keywords2(keywords,
                                        keywords_val->data,
                                        rev_str, url, changed_date,
                                        author, result_pool));
    }

  eol_style_val = apr_hash_get(props, SVN_PROP_EOL_STYLE,
                               APR_HASH_KEY_STRING);
  if (eol_style_val)
    {
      svn_subst_eol_style_from_value(eol_style,
                                     eol_str,
                                     eol_style_val->data);
    }

  return SVN_NO_ERROR;
}

/* Resolve the exact path for a patch TARGET at path PATH_FROM_PATCHFILE,
 * which is the path of the target as it appeared in the patch file.
 * Put a canonicalized version of PATH_FROM_PATCHFILE into
 * TARGET->CANON_PATH_FROM_PATCHFILE.
 * WC_CTX is a context for the working copy the patch is applied to.
 * If possible, determine TARGET->WC_PATH, TARGET->ABS_PATH, TARGET->KIND,
 * TARGET->ADDED, and TARGET->PARENT_DIR_EXISTS.
 * Indicate in TARGET->SKIPPED whether the target should be skipped.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * PROP_CHANGES_ONLY specifies whether the target path is allowed to have
 * only property changes, and no content changes (in which case the target
 * must be a directory).
 * Use RESULT_POOL for allocations of fields in TARGET.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
resolve_target_path(patch_target_t *target,
                    const char *path_from_patchfile,
                    const char *local_abspath,
                    int strip_count,
                    svn_boolean_t prop_changes_only,
                    svn_wc_context_t *wc_ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *stripped_path;
  const char *full_path;
  svn_wc_status3_t *status;
  svn_error_t *err;
  svn_boolean_t under_root;

  target->canon_path_from_patchfile = svn_dirent_internal_style(
                                        path_from_patchfile, result_pool);

  /* We allow properties to be set on the wc root dir.
   * ### Do we need to check for empty paths here, shouldn't the parser
   * ### guarentee that the paths returned are non-empty? */
  if (! prop_changes_only && target->canon_path_from_patchfile[0] == '\0')
    {
      /* An empty patch target path? What gives? Skip this. */
      target->skipped = TRUE;
      target->local_abspath = NULL;
      target->local_relpath = "";
      return SVN_NO_ERROR;
    }

  if (strip_count > 0)
    SVN_ERR(strip_path(&stripped_path, target->canon_path_from_patchfile,
                       strip_count, result_pool, scratch_pool));
  else
    stripped_path = target->canon_path_from_patchfile;

  if (svn_dirent_is_absolute(stripped_path))
    {
      target->local_relpath = svn_dirent_is_child(local_abspath, stripped_path,
                                                  result_pool);

      if (! target->local_relpath)
        {
          /* The target path is either outside of the working copy
           * or it is the working copy itself. Skip it. */
          target->skipped = TRUE;
          target->local_abspath = NULL;
          target->local_relpath = stripped_path;
          return SVN_NO_ERROR;
        }
    }
  else
    {
      target->local_relpath = stripped_path;
    }

  /* Make sure the path is secure to use. We want the target to be inside
   * of the working copy and not be fooled by symlinks it might contain. */
  SVN_ERR(svn_dirent_is_under_root(&under_root,
                                   &full_path, local_abspath,
                                   target->local_relpath, result_pool));

  if (! under_root)
    {
      /* The target path is outside of the working copy. Skip it. */
      target->skipped = TRUE;
      target->local_abspath = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_dirent_get_absolute(&target->local_abspath, full_path,
                                  result_pool));

  /* Skip things we should not be messing with. */
  err = svn_wc_status3(&status, wc_ctx, target->local_abspath,
                       result_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        svn_error_clear(err);
      else
        return svn_error_return(err);
    }
  else if (status->node_status == svn_wc_status_ignored ||
           status->node_status == svn_wc_status_unversioned ||
           status->node_status == svn_wc_status_missing ||
           status->node_status == svn_wc_status_obstructed)
    {
      target->skipped = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_check_path(target->local_abspath,
                            &target->kind_on_disk, scratch_pool));
  err = svn_wc__node_is_status_deleted(&target->locally_deleted,
                                       wc_ctx, target->local_abspath,
                                       scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          target->locally_deleted = FALSE;
        }
      else
        return svn_error_return(err);
    }
  SVN_ERR(svn_wc_read_kind(&target->db_kind, wc_ctx, target->local_abspath,
                           FALSE, scratch_pool));

  /* If the target is a versioned directory present on disk,
   * and there are only property changes in the patch, we accept
   * a directory target. Else, we skip directories. */
  if (target->db_kind == svn_node_dir && ! prop_changes_only)
    {
      /* ### We cannot yet replace a locally deleted dir with a file,
       * ### but some day we might want to allow it. */
      target->skipped = TRUE;
      return SVN_NO_ERROR;
    }

  /* ### Shouldn't libsvn_wc flag an obstruction in this case? */
  if (target->locally_deleted && target->kind_on_disk != svn_node_none)
    {
      target->skipped = TRUE;
      return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}

/* Initialize a PROP_TARGET structure for PROP_NAME. Use working copy
 * context WC_CTX. Use the REJECT stream that is shared with all content
 * info structures.
 * REMOVE_TEMPFILES as in svn_client_patch().
 */
static svn_error_t *
init_prop_target(prop_patch_target_t **prop_target,
                 const char *prop_name,
                 svn_diff_operation_kind_t operation,
                 svn_stream_t *reject,
                 svn_boolean_t remove_tempfiles,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  prop_patch_target_t *new_prop_target;
  target_content_info_t *content_info; 
  const svn_string_t *value;
  const char *patched_path;
  svn_error_t *err;

  content_info = apr_pcalloc(result_pool, sizeof(*content_info));

  /* All other fields in are FALSE or NULL due to apr_pcalloc().*/
  content_info->current_line = 1;
  content_info->eol_style = svn_subst_eol_style_none;
  content_info->lines = apr_array_make(result_pool, 0,
                                       sizeof(svn_stream_mark_t *));
  content_info->hunks = apr_array_make(result_pool, 0, sizeof(hunk_info_t *));
  content_info->keywords = apr_hash_make(result_pool);
  content_info->reject = reject;
  content_info->pool = result_pool;

  new_prop_target = apr_palloc(result_pool, sizeof(*new_prop_target));
  new_prop_target->name = apr_pstrdup(result_pool, prop_name);
  new_prop_target->operation = operation;
  new_prop_target->content_info = content_info;

  err = svn_wc_prop_get2(&value, wc_ctx, local_abspath, prop_name, 
                           result_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          value = NULL;
        }
      else
        return svn_error_return(err);
    }

  if (value)
    content_info->stream = svn_stream_from_string(value, result_pool);

  /* Create a temporary file to write the patched result to. For properties,
   * we don't have to worry about eol-style. ### Why not? */
  SVN_ERR(svn_stream_open_unique(&content_info->patched,
                                 &patched_path, NULL,
                                 remove_tempfiles ?
                                   svn_io_file_del_on_pool_cleanup :
                                   svn_io_file_del_none,
                                 result_pool, scratch_pool));

  new_prop_target->patched_path = patched_path;
  *prop_target = new_prop_target;

  return SVN_NO_ERROR;
}

/* Return a suitable filename for the target of PATCH.
 * Examine the ``old'' and ``new'' file names, and choose the file name
 * with the fewest path components, the shortest basename, and the shortest
 * total file name length (in that order). In case of a tie, return the new
 * filename. This heuristic is also used by Larry Wall's UNIX patch (except
 * that it prompts for a filename in case of a tie). */
static const char *
choose_target_filename(const svn_patch_t *patch)
{
  apr_size_t old;
  apr_size_t new;

  old = svn_path_component_count(patch->old_filename);
  new = svn_path_component_count(patch->new_filename);

  if (old == new)
    {
      old = strlen(svn_dirent_basename(patch->old_filename, NULL));
      new = strlen(svn_dirent_basename(patch->new_filename, NULL));

      if (old == new)
        {
          old = strlen(patch->old_filename);
          new = strlen(patch->new_filename);
        }
    }

  return (old < new) ? patch->old_filename : patch->new_filename;
}

/* Attempt to initialize a *PATCH_TARGET structure for a target file
 * described by PATCH. Use working copy context WC_CTX.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * The patch target structure is allocated in RESULT_POOL, but if the target
 * should be skipped, PATCH_TARGET->SKIPPED is set and the target should be
 * treated as not fully initialized, e.g. the caller should not not do any
 * further operations on the target if it is marked to be skipped.
 * If REMOVE_TEMPFILES is TRUE, set up temporary files to be removed as
 * soon as they are no longer needed.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
init_patch_target(patch_target_t **patch_target,
                  const svn_patch_t *patch,
                  const char *base_dir,
                  svn_wc_context_t *wc_ctx, int strip_count,
                  svn_boolean_t remove_tempfiles,
                  apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;
  target_content_info_t *content_info; 
  svn_boolean_t has_prop_changes = FALSE;
  svn_boolean_t prop_changes_only = FALSE;

  {
    apr_hash_index_t *hi;

    for (hi = apr_hash_first(scratch_pool, patch->prop_patches);
         hi;
         hi = apr_hash_next(hi))
      {
        svn_prop_patch_t *prop_patch = svn__apr_hash_index_val(hi); 
        if (! has_prop_changes)
          has_prop_changes = prop_patch->hunks->nelts > 0;
        else
          break;
      }
  }

  prop_changes_only = has_prop_changes && patch->hunks->nelts == 0;

  content_info = apr_pcalloc(result_pool, sizeof(*content_info));

  /* All other fields in content_info are FALSE or NULL due to apr_pcalloc().*/
  content_info->current_line = 1;
  content_info->eol_style = svn_subst_eol_style_none;
  content_info->lines = apr_array_make(result_pool, 0,
                                       sizeof(svn_stream_mark_t *));
  content_info->hunks = apr_array_make(result_pool, 0, sizeof(hunk_info_t *));
  content_info->keywords = apr_hash_make(result_pool);
  content_info->pool = result_pool;

  target = apr_pcalloc(result_pool, sizeof(*target));

  /* All other fields in target are FALSE or NULL due to apr_pcalloc(). */
  target->db_kind = svn_node_none;
  target->kind_on_disk = svn_node_none;
  target->content_info = content_info;
  target->prop_targets = apr_hash_make(result_pool);
  target->pool = result_pool;

  SVN_ERR(resolve_target_path(target, choose_target_filename(patch),
                              base_dir, strip_count, prop_changes_only,
                              wc_ctx, result_pool, scratch_pool));
  if (! target->skipped)
    {
      const char *diff_header;
      svn_boolean_t repair_eol;
      apr_size_t len;
      svn_stream_t *patched_raw;

      /* Create a temporary file, and associated streams,
       * to write the patched result to. */
      if (target->kind_on_disk == svn_node_file)
        {
          SVN_ERR(svn_io_file_open(&target->file, target->local_abspath,
                                   APR_READ | APR_BINARY | APR_BUFFERED,
                                   APR_OS_DEFAULT, result_pool));

          content_info->stream = svn_stream_from_aprfile2(target->file,
                                                          FALSE, result_pool);

          SVN_ERR(svn_wc_text_modified_p2(&target->local_mods, wc_ctx,
                                          target->local_abspath, FALSE,
                                          scratch_pool));
          SVN_ERR(svn_io_is_file_executable(&target->executable,
                                            target->local_abspath,
                                            scratch_pool));

          SVN_ERR(obtain_eol_and_keywords_for_file(&content_info->keywords,
                                                   &content_info->eol_style,
                                                   &content_info->eol_str,
                                                   wc_ctx,
                                                   target->local_abspath,
                                                   result_pool,
                                                   scratch_pool));
        }

      /* ### Is it ok to set the operation of the target already here? Isn't
       * ### the target supposed to be marked with an operation after we have
       * ### determined that the changes will apply cleanly to the WC? Maybe
       * ### we should have kept the patch field in patch_target_t to be
       * ### able to distinguish between 'what the patch says we should do' 
       * ### and 'what we can do with the given state of our WC'. */
      if (patch->operation == svn_diff_op_added)
        target->added = TRUE;
      else if (patch->operation == svn_diff_op_deleted)
        target->deleted = TRUE;

      SVN_ERR(svn_stream_open_unique(&patched_raw,
                                     &target->patched_path, NULL,
                                     remove_tempfiles ?
                                       svn_io_file_del_on_pool_cleanup :
                                       svn_io_file_del_none,
                                     result_pool, scratch_pool));

      /* We always expand keywords in the patched file, but repair newlines
       * only if svn:eol-style dictates a particular style. */
      repair_eol = (content_info->eol_style == svn_subst_eol_style_fixed 
                    || content_info->eol_style == svn_subst_eol_style_native);
      content_info->patched = svn_subst_stream_translated(
                              patched_raw, content_info->eol_str, repair_eol,
                              content_info->keywords, TRUE, result_pool);

      /* We don't expand keywords, nor normalise line-endings,
       * in reject files. */
      SVN_ERR(svn_stream_open_unique(&content_info->reject,
                                     &target->reject_path, NULL,
                                     remove_tempfiles ?
                                       svn_io_file_del_on_pool_cleanup :
                                       svn_io_file_del_none,
                                     result_pool, scratch_pool));

      /* The reject stream needs a diff header. */
      diff_header = apr_psprintf(scratch_pool, "--- %s%s+++ %s%s",
                                 target->canon_path_from_patchfile,
                                 APR_EOL_STR,
                                 target->canon_path_from_patchfile,
                                 APR_EOL_STR);
      len = strlen(diff_header);
      SVN_ERR(svn_stream_write(content_info->reject, diff_header, &len));

      /* Handle properties. */
      if (! target->skipped)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(result_pool, patch->prop_patches);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *prop_name = svn__apr_hash_index_key(hi);
              svn_prop_patch_t *prop_patch = svn__apr_hash_index_val(hi);
              prop_patch_target_t *prop_target;

              SVN_ERR(init_prop_target(&prop_target,
                                       prop_name,
                                       prop_patch->operation,
                                       content_info->reject,
                                       remove_tempfiles,
                                       wc_ctx, target->local_abspath,
                                       result_pool, scratch_pool));

              apr_hash_set(target->prop_targets, prop_name,
                           APR_HASH_KEY_STRING, prop_target);
            }
        }
    }

  *patch_target = target;
  return SVN_NO_ERROR;
}

/* Read a *LINE from CONTENT_INFO. If the line has not been read before
 * mark the line in CONTENT_INFO->LINES. Allocate *LINE in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
read_line(target_content_info_t *content_info,
          const char **line,
          apr_pool_t *scratch_pool,
          apr_pool_t *result_pool)
{
  svn_stringbuf_t *line_raw;
  const char *eol_str;

  if (content_info->eof)
    {
      *line = "";
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(content_info->current_line <= content_info->lines->nelts + 1);
  if (content_info->current_line == content_info->lines->nelts + 1)
    {
      svn_stream_mark_t *mark;
      SVN_ERR(svn_stream_mark(content_info->stream, &mark,
                              content_info->pool));
      APR_ARRAY_PUSH(content_info->lines, svn_stream_mark_t *) = mark;
    }

  SVN_ERR(svn_stream_readline_detect_eol(content_info->stream, &line_raw,
                                         &eol_str, &content_info->eof,
                                         scratch_pool));
  if (content_info->eol_style == svn_subst_eol_style_none)
    content_info->eol_str = eol_str;

  /* Contract keywords. */
  SVN_ERR(svn_subst_translate_cstring2(line_raw->data, line,
                                       NULL, FALSE,
                                       content_info->keywords, FALSE,
                                       result_pool));
  if (! content_info->eof)
    content_info->current_line++;

  return SVN_NO_ERROR;
}

/* Seek to the specified LINE in CONTENT_INFO.
 * Mark any lines not read before in CONTENT_INFO->LINES.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
seek_to_line(target_content_info_t *content_info, svn_linenum_t line,
             apr_pool_t *scratch_pool)
{
  svn_linenum_t saved_line;
  svn_boolean_t saved_eof;

  SVN_ERR_ASSERT(line > 0);

  if (line == content_info->current_line)
    return SVN_NO_ERROR;

  saved_line = content_info->current_line;
  saved_eof = content_info->eof;

  if (line <= content_info->lines->nelts)
    {
      svn_stream_mark_t *mark;

      mark = APR_ARRAY_IDX(content_info->lines, line - 1, svn_stream_mark_t *);
      SVN_ERR(svn_stream_seek(content_info->stream, mark));
      content_info->current_line = line;
    }
  else
    {
      const char *dummy;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      while (! content_info->eof && content_info->current_line < line)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(read_line(content_info, &dummy, iterpool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  /* After seeking backwards from EOF position clear EOF indicator. */
  if (saved_eof && saved_line > content_info->current_line)
    content_info->eof = FALSE;

  return SVN_NO_ERROR;
}

/* Indicate in *MATCHED whether the original text of HUNK matches the patch
 * CONTENT_INFO at its current line. Lines within FUZZ lines of the start or
 * end of HUNK will always match. If IGNORE_WHITESPACE is set, we ignore
 * whitespace when doing the matching. When this function returns, neither
 * CONTENT_INFO->CURRENT_LINE nor the file offset in the target file will 
 * have changed. If MATCH_MODIFIED is TRUE, match the modified hunk text,
 * rather than the original hunk text.
 * Do temporary allocations in POOL. */
static svn_error_t *
match_hunk(svn_boolean_t *matched, target_content_info_t *content_info,
           const svn_diff_hunk_t *hunk, int fuzz,
           svn_boolean_t ignore_whitespace,
           svn_boolean_t match_modified, apr_pool_t *pool)
{
  svn_stringbuf_t *hunk_line;
  const char *target_line;
  svn_linenum_t lines_read;
  svn_linenum_t saved_line;
  svn_boolean_t hunk_eof;
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;
  svn_linenum_t hunk_length;
  svn_linenum_t leading_context;
  svn_linenum_t trailing_context;

  *matched = FALSE;

  if (content_info->eof)
    return SVN_NO_ERROR;

  saved_line = content_info->current_line;
  lines_read = 0;
  lines_matched = FALSE;
  leading_context = svn_diff_hunk_get_leading_context(hunk);
  trailing_context = svn_diff_hunk_get_trailing_context(hunk);
  if (match_modified)
    {
      SVN_ERR(svn_diff_hunk_reset_modified_text(hunk));
      hunk_length = svn_diff_hunk_get_modified_length(hunk);
    }
  else
    {
      SVN_ERR(svn_diff_hunk_reset_original_text(hunk));
      hunk_length = svn_diff_hunk_get_original_length(hunk);
    }
  iterpool = svn_pool_create(pool);
  do
    {
      const char *hunk_line_translated;

      svn_pool_clear(iterpool);

      if (match_modified)
        SVN_ERR(svn_diff_hunk_readline_modified_text(hunk, &hunk_line,
                                                     NULL, &hunk_eof,
                                                     iterpool, iterpool));
      else
        SVN_ERR(svn_diff_hunk_readline_original_text(hunk, &hunk_line,
                                                     NULL, &hunk_eof,
                                                     iterpool, iterpool));

      /* Contract keywords, if any, before matching. */
      SVN_ERR(svn_subst_translate_cstring2(hunk_line->data,
                                           &hunk_line_translated,
                                           NULL, FALSE,
                                           content_info->keywords, FALSE,
                                           iterpool));
      SVN_ERR(read_line(content_info, &target_line, iterpool, iterpool));

      lines_read++;

      /* If the last line doesn't have a newline, we get EOF but still
       * have a non-empty line to compare. */
      if ((hunk_eof && hunk_line->len == 0) ||
          (content_info->eof && *target_line == 0))
        break;

      /* Leading/trailing fuzzy lines always match. */
      if ((lines_read <= fuzz && leading_context > fuzz) ||
          (lines_read > hunk_length - fuzz && trailing_context > fuzz))
        lines_matched = TRUE;
      else
        {
          if (ignore_whitespace)
            {
              char *hunk_line_trimmed;
              char *target_line_trimmed;

              hunk_line_trimmed = apr_pstrdup(iterpool, hunk_line_translated);
              target_line_trimmed = apr_pstrdup(iterpool, target_line);
              apr_collapse_spaces(hunk_line_trimmed, hunk_line_trimmed);
              apr_collapse_spaces(target_line_trimmed, target_line_trimmed);
              lines_matched = ! strcmp(hunk_line_trimmed, target_line_trimmed);
            }
          else
            lines_matched = ! strcmp(hunk_line_translated, target_line);
        }
    }
  while (lines_matched);

  *matched = lines_matched && hunk_eof && hunk_line->len == 0;

  SVN_ERR(seek_to_line(content_info, saved_line, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Scan lines of CONTENT_INFO for a match of the original text of HUNK,
 * up to but not including the specified UPPER_LINE. Use fuzz factor FUZZ.
 * If UPPER_LINE is zero scan until EOF occurs when reading from TARGET.
 * Return the line at which HUNK was matched in *MATCHED_LINE.
 * If the hunk did not match at all, set *MATCHED_LINE to zero.
 * If the hunk matched multiple times, and MATCH_FIRST is TRUE,
 * return the line number at which the first match occured in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is FALSE,
 * return the line number at which the last match occured in *MATCHED_LINE.
 * If IGNORE_WHITESPACE is set, ignore whitespace during the matching.
 * If MATCH_MODIFIED is TRUE, match the modified hunk text,
 * rather than the original hunk text.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do all allocations in POOL. */
static svn_error_t *
scan_for_match(svn_linenum_t *matched_line, 
               target_content_info_t *content_info,
               const svn_diff_hunk_t *hunk, svn_boolean_t match_first,
               svn_linenum_t upper_line, int fuzz, 
               svn_boolean_t ignore_whitespace,
               svn_boolean_t match_modified,
               svn_cancel_func_t cancel_func, void *cancel_baton,
               apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  *matched_line = 0;
  iterpool = svn_pool_create(pool);
  while ((content_info->current_line < upper_line || upper_line == 0) &&
         ! content_info->eof)
    {
      svn_boolean_t matched;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR((cancel_func)(cancel_baton));

      SVN_ERR(match_hunk(&matched, content_info, hunk, fuzz, ignore_whitespace,
                         match_modified, iterpool));
      if (matched)
        {
          svn_boolean_t taken = FALSE;
          int i;

          /* Don't allow hunks to match at overlapping locations. */
          for (i = 0; i < content_info->hunks->nelts; i++)
            {
              const hunk_info_t *hi;
              svn_linenum_t length;

              hi = APR_ARRAY_IDX(content_info->hunks, i, const hunk_info_t *);

              if (match_modified)
                length = svn_diff_hunk_get_modified_length(hi->hunk);
              else
                length = svn_diff_hunk_get_original_length(hi->hunk);

              taken = (! hi->rejected &&
                       content_info->current_line >= hi->matched_line &&
                       content_info->current_line < (hi->matched_line + length));
              if (taken)
                break;
            }

          if (! taken)
            {
              *matched_line = content_info->current_line;
              if (match_first)
                break;
            }
        }

      if (! content_info->eof)
        SVN_ERR(seek_to_line(content_info, content_info->current_line + 1,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Indicate in *MATCH whether the STREAM matches the modified text of HUNK.
 * Use CONTENT_INFO for retrieving information on eol and keywords that is
 * needed for the comparison.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
match_existing_target(svn_boolean_t *match,
                      target_content_info_t *content_info,
                      const svn_diff_hunk_t *hunk,
                      svn_stream_t *stream,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;
  svn_boolean_t eof;
  svn_boolean_t hunk_eof;

  SVN_ERR(svn_diff_hunk_reset_modified_text(hunk));

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;
      svn_stringbuf_t *hunk_line;
      const char *line_translated;
      const char *hunk_line_translated;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(stream, &line, NULL,
                                             &eof, iterpool));
      SVN_ERR(svn_diff_hunk_readline_modified_text(hunk, &hunk_line,
                                                   NULL, &hunk_eof,
                                                   iterpool, iterpool));
      /* Contract keywords. */
      SVN_ERR(svn_subst_translate_cstring2(line->data, &line_translated,
                                           NULL, FALSE, 
                                           content_info->keywords,
                                           FALSE, iterpool));
      SVN_ERR(svn_subst_translate_cstring2(hunk_line->data,
                                           &hunk_line_translated,
                                           NULL, FALSE,
                                           content_info->keywords,
                                           FALSE, iterpool));
      lines_matched = ! strcmp(line_translated, hunk_line_translated);
      if (eof != hunk_eof)
        {
          svn_pool_destroy(iterpool);
          *match = FALSE;
          return SVN_NO_ERROR;
        }
      }
    while (lines_matched && ! eof && ! hunk_eof);
    svn_pool_destroy(iterpool);

    *match = (lines_matched && eof == hunk_eof);
    return SVN_NO_ERROR;
}

/* Determine the line at which a HUNK applies to the CONTENT_INFO of TARGET
 * file, and return an appropriate hunk_info object in *HI, allocated from
 * RESULT_POOL. Use fuzz factor FUZZ. Set HI->FUZZ to FUZZ. If no correct
 * line can be determined, set HI->REJECTED to TRUE.
 * IGNORE_WHITESPACE tells whether whitespace should be considered when
 * matching. IS_PROP_HUNK indicates whether the hunk patches file content
 * or a property.
 * When this function returns, neither CONTENT_INFO->CURRENT_LINE nor
 * the file offset in the target file will have changed.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do temporary allocations in POOL. */
static svn_error_t *
get_hunk_info(hunk_info_t **hi, patch_target_t *target,
              target_content_info_t *content_info,
              const svn_diff_hunk_t *hunk, int fuzz,
              svn_boolean_t ignore_whitespace,
              svn_boolean_t is_prop_hunk,
              svn_cancel_func_t cancel_func, void *cancel_baton,
              apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  svn_linenum_t matched_line;
  svn_linenum_t original_start;
  svn_boolean_t already_applied;

  original_start = svn_diff_hunk_get_original_start(hunk);
  already_applied = FALSE;

  /* An original offset of zero means that this hunk wants to create
   * a new file. Don't bother matching hunks in that case, since
   * the hunk applies at line 1. If the file already exists, the hunk
   * is rejected, unless the file is versioned and its content matches
   * the file the patch wants to create.  */
  if (original_start == 0 && ! is_prop_hunk)
    {
      if (target->kind_on_disk == svn_node_file)
        {
          if (target->db_kind == svn_node_file)
            {
              svn_boolean_t file_matches;
              apr_file_t *file;
              svn_stream_t *stream;

              /* ### dannas: Why not use content_info-stream here? */
              SVN_ERR(svn_io_file_open(&file, target->local_abspath,
                                       APR_READ | APR_BINARY, APR_OS_DEFAULT,
                                       scratch_pool));
              stream = svn_stream_from_aprfile2(file, FALSE, scratch_pool);

              SVN_ERR(match_existing_target(&file_matches, content_info, hunk,
                                            stream, scratch_pool));
              SVN_ERR(svn_stream_close(stream));

              if (file_matches)
                {
                  matched_line = 1;
                  already_applied = TRUE;
                }
              else
                matched_line = 0; /* reject */
            }
          else
            matched_line = 0; /* reject */
        }
      else
        matched_line = 1;
    }
  /* Same conditions apply as for the file case above. 
   *
   * ### Since the hunk says the prop should be added we just assume so for
   * ### now and don't bother with storing the previous lines and such. When
   * ### we have the diff operation available we can just check for adds. */
  else if (original_start == 0 && is_prop_hunk)
    {
      if (content_info->stream)
        {
          svn_boolean_t prop_matches;

          SVN_ERR(match_existing_target(&prop_matches, content_info, hunk,
                                        content_info->stream, scratch_pool));

          if (prop_matches)
            {
              matched_line = 1;
              already_applied = TRUE;
            }
          else
            matched_line = 0; /* reject */
        }
      else
        matched_line = 1;
    }
  /* ### We previously checked for target->kind_on_disk == svn_node_file
   * ### here. But that wasn't generic enough to cope with properties. How
   * ### better describe that content_info->stream is only set if we have an
   * ### existing target? */
  else if (original_start > 0 && content_info->stream)
    {
      svn_linenum_t saved_line = content_info->current_line;

      /* Scan for a match at the line where the hunk thinks it
       * should be going. */
      SVN_ERR(seek_to_line(content_info, original_start, scratch_pool));
      if (content_info->current_line != original_start)
        {
          /* Seek failed. */
          matched_line = 0;
        }
      else
        SVN_ERR(scan_for_match(&matched_line, content_info, hunk, TRUE,
                               original_start + 1, fuzz,
                               ignore_whitespace, FALSE,
                               cancel_func, cancel_baton,
                               scratch_pool));

      if (matched_line != original_start)
        {
          /* Check if the hunk is already applied.
           * We only check for an exact match here, and don't bother checking
           * for already applied patches with offset/fuzz, because such a
           * check would be ambiguous. */
          if (fuzz == 0)
            {
              svn_linenum_t modified_start;

              modified_start = svn_diff_hunk_get_modified_start(hunk);
              if (modified_start == 0)
                {
                  /* Patch wants to delete the file. */
                  already_applied = target->locally_deleted;
                }
              else
                {
                  SVN_ERR(seek_to_line(content_info, modified_start,
                                       scratch_pool));
                  SVN_ERR(scan_for_match(&matched_line, content_info,
                                         hunk, TRUE,
                                         modified_start + 1,
                                         fuzz, ignore_whitespace, TRUE,
                                         cancel_func, cancel_baton,
                                         scratch_pool));
                  already_applied = (matched_line == modified_start);
                }
            }
          else
            already_applied = FALSE;

          if (! already_applied)
            {
              /* Scan the whole file again from the start. */
              SVN_ERR(seek_to_line(content_info, 1, scratch_pool));

              /* Scan forward towards the hunk's line and look for a line
               * where the hunk matches. */
              SVN_ERR(scan_for_match(&matched_line, content_info, hunk, FALSE,
                                     original_start, fuzz,
                                     ignore_whitespace, FALSE,
                                     cancel_func, cancel_baton,
                                     scratch_pool));

              /* In tie-break situations, we arbitrarily prefer early matches
               * to save us from scanning the rest of the file. */
              if (matched_line == 0)
                {
                  /* Scan forward towards the end of the file and look
                   * for a line where the hunk matches. */
                  SVN_ERR(scan_for_match(&matched_line, content_info, hunk,
                                         TRUE, 0, fuzz, ignore_whitespace,
                                         FALSE, cancel_func, cancel_baton,
                                         scratch_pool));
                }
            }
        }

      SVN_ERR(seek_to_line(content_info, saved_line, scratch_pool));
    }
  else
    {
      /* The hunk wants to modify a file which doesn't exist. */
      matched_line = 0;
    }

  (*hi) = apr_palloc(result_pool, sizeof(hunk_info_t));
  (*hi)->hunk = hunk;
  (*hi)->matched_line = matched_line;
  (*hi)->rejected = (matched_line == 0);
  (*hi)->already_applied = already_applied;
  (*hi)->fuzz = fuzz;

  return SVN_NO_ERROR;
}

/* Copy lines to the patched stream until the specified LINE has been
 * reached. Indicate in *EOF whether end-of-file was encountered while
 * reading from the target.
 * If LINE is zero, copy lines until end-of-file has been reached.
 * Do all allocations in POOL. */
static svn_error_t *
copy_lines_to_target(target_content_info_t *content_info, svn_linenum_t line,
                     const char *patched_path, apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(pool);
  while ((content_info->current_line < line || line == 0) 
         && ! content_info->eof)
    {
      const char *target_line;
      apr_size_t len;

      svn_pool_clear(iterpool);

      SVN_ERR(read_line(content_info, &target_line, iterpool, iterpool));
      if (! content_info->eof)
        target_line = apr_pstrcat(iterpool, target_line, content_info->eol_str,
                                  (char *)NULL);
      len = strlen(target_line);
      SVN_ERR(svn_stream_write(content_info->patched, target_line, &len));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Write the diff text of the hunk described by HI to the
 * reject stream of CONTENT_INFO, and mark TARGET as having had rejects.
 * Do temporary allocations in POOL. */
static svn_error_t *
reject_hunk(patch_target_t *target, target_content_info_t *content_info, 
            hunk_info_t *hi, const char *prop_name, apr_pool_t *pool)
{
  const char *hunk_header;
  apr_size_t len;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  if (prop_name)
    {
      const char *prop_header;
        
      /* ### Print 'Added', 'Deleted' or 'Modified' instead of 'Propperty'.
       */
      prop_header = apr_psprintf(pool, "Property: %s\n", prop_name);
      len = strlen(prop_header);

      SVN_ERR(svn_stream_write(content_info->reject, prop_header, &len));

      /* ### What about just setting a variable to either "@@" or "##",
       * ### and merging with the else clause below? */
      hunk_header = apr_psprintf(pool, "## -%lu,%lu +%lu,%lu ##%s",
                                 svn_diff_hunk_get_original_start(hi->hunk),
                                 svn_diff_hunk_get_original_length(hi->hunk),
                                 svn_diff_hunk_get_modified_start(hi->hunk),
                                 svn_diff_hunk_get_modified_length(hi->hunk),
                                 APR_EOL_STR);
    }
  else
    hunk_header = apr_psprintf(pool, "@@ -%lu,%lu +%lu,%lu @@%s",
                               svn_diff_hunk_get_original_start(hi->hunk),
                               svn_diff_hunk_get_original_length(hi->hunk),
                               svn_diff_hunk_get_modified_start(hi->hunk),
                               svn_diff_hunk_get_modified_length(hi->hunk),
                               APR_EOL_STR);
  len = strlen(hunk_header);
  SVN_ERR(svn_stream_write(content_info->reject, hunk_header, &len));

  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_diff_hunk_readline_diff_text(hi->hunk, &hunk_line, &eol_str,
                                               &eof, iterpool, iterpool));
      if (! eof)
        {
          if (hunk_line->len >= 1)
            {
              len = hunk_line->len;
              SVN_ERR(svn_stream_write(content_info->reject, hunk_line->data,
                                       &len));
            }

          if (eol_str)
            {
              len = strlen(eol_str);
              SVN_ERR(svn_stream_write(content_info->reject, eol_str, &len));
            }
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  if (prop_name)
    target->had_prop_rejects = TRUE;
  else
    target->had_rejects = TRUE;

  return SVN_NO_ERROR;
}

/* Write the modified text of the hunk described by HI to the patched
 * stream of CONTENT_INFO. TARGET is the patch target.
 * If PROP_NAME is not NULL, the hunk is assumed to be targeted for
 * a property with the given name.
 * Do temporary allocations in POOL. */
static svn_error_t *
apply_hunk(patch_target_t *target, target_content_info_t *content_info,  
           hunk_info_t *hi, const char *prop_name, apr_pool_t *pool)
{
  svn_linenum_t lines_read;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  /* ### Is there a cleaner way to describe if we have an existing target?
   */
  if (target->kind_on_disk == svn_node_file || prop_name)
    {
      svn_linenum_t line;

      /* Move forward to the hunk's line, copying data as we go.
       * Also copy leading lines of context which matched with fuzz.
       * The target has changed on the fuzzy-matched lines,
       * so we should retain the target's version of those lines. */
      SVN_ERR(copy_lines_to_target(content_info, hi->matched_line + hi->fuzz,
                                   target->patched_path, pool));

      /* Skip the target's version of the hunk.
       * Don't skip trailing lines which matched with fuzz. */
      line = content_info->current_line +
             svn_diff_hunk_get_original_length(hi->hunk) - (2 * hi->fuzz);
      SVN_ERR(seek_to_line(content_info, line, pool));
      if (content_info->current_line != line && ! content_info->eof)
        {
          /* Seek failed, reject this hunk. */
          hi->rejected = TRUE;
          SVN_ERR(reject_hunk(target, content_info, hi, prop_name, pool));
          return SVN_NO_ERROR;
        }
    }

  /* Write the hunk's version to the patched result.
   * Don't write the lines which matched with fuzz. */
  lines_read = 0;
  SVN_ERR(svn_diff_hunk_reset_modified_text(hi->hunk));
  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_diff_hunk_readline_modified_text(hi->hunk, &hunk_line,
                                                   &eol_str, &eof,
                                                   iterpool, iterpool));
      lines_read++;
      if (! eof && lines_read > hi->fuzz &&
          lines_read <= svn_diff_hunk_get_modified_length(hi->hunk) - hi->fuzz)
        {
          apr_size_t len;

          if (hunk_line->len >= 1)
            {
              len = hunk_line->len;
              SVN_ERR(svn_stream_write(content_info->patched, hunk_line->data,
                                       &len));
            }

          if (eol_str)
            {
              /* Use the EOL as it was read from the patch file,
               * unless the target's EOL style is set by svn:eol-style */
              if (content_info->eol_style != svn_subst_eol_style_none)
                eol_str = content_info->eol_str;

              len = strlen(eol_str);
              SVN_ERR(svn_stream_write(content_info->patched, eol_str, &len));
            }
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  if (prop_name)
    target->has_prop_changes = TRUE;
  else
    target->has_text_changes = TRUE;

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for hunk HI,
 * using TARGET to determine the path. If the hunk is a property hunk,
 * PROP_NAME must be the name of the property, else NULL.
 * Use POOL for temporary allocations. */
static svn_error_t *
send_hunk_notification(const hunk_info_t *hi, 
                       const patch_target_t *target, 
                       const char *prop_name, 
                       const svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  svn_wc_notify_t *notify;
  svn_wc_notify_action_t action;

  if (hi->already_applied)
    action = svn_wc_notify_patch_hunk_already_applied;
  else if (hi->rejected)
    action = svn_wc_notify_patch_rejected_hunk;
  else
    action = svn_wc_notify_patch_applied_hunk;

  notify = svn_wc_create_notify(target->local_abspath
                                    ? target->local_abspath
                                    : target->local_relpath,
                                action, pool);
  notify->hunk_original_start =
    svn_diff_hunk_get_original_start(hi->hunk);
  notify->hunk_original_length =
    svn_diff_hunk_get_original_length(hi->hunk);
  notify->hunk_modified_start =
    svn_diff_hunk_get_modified_start(hi->hunk);
  notify->hunk_modified_length =
    svn_diff_hunk_get_modified_length(hi->hunk);
  notify->hunk_matched_line = hi->matched_line;
  notify->hunk_fuzz = hi->fuzz;
  notify->prop_name = prop_name;

  (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for a patch TARGET.
 * Use POOL for temporary allocations. */
static svn_error_t *
send_patch_notification(const patch_target_t *target,
                        const svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_wc_notify_t *notify;
  svn_wc_notify_action_t action;

  if (! ctx->notify_func2)
    return SVN_NO_ERROR;

  if (target->skipped)
    action = svn_wc_notify_skip;
  else if (target->deleted)
    action = svn_wc_notify_delete;
  else if (target->added || target->replaced)
    action = svn_wc_notify_add;
  else
    action = svn_wc_notify_patch;

  notify = svn_wc_create_notify(target->local_abspath ? target->local_abspath
                                                 : target->local_relpath,
                                action, pool);
  notify->kind = svn_node_file;

  if (action == svn_wc_notify_skip)
    {
      if (target->db_kind == svn_node_none ||
          target->db_kind == svn_node_unknown)
        notify->content_state = svn_wc_notify_state_missing;
      else if (target->db_kind == svn_node_dir)
        notify->content_state = svn_wc_notify_state_obstructed;
      else
        notify->content_state = svn_wc_notify_state_unknown;
    }
  else
    {
      if (target->had_rejects)
        notify->content_state = svn_wc_notify_state_conflicted;
      else if (target->local_mods)
        notify->content_state = svn_wc_notify_state_merged;
      else if (target->has_text_changes)
        notify->content_state = svn_wc_notify_state_changed;

      if (target->had_prop_rejects)
        notify->prop_state = svn_wc_notify_state_conflicted;
      else if (target->has_prop_changes)
        notify->prop_state = svn_wc_notify_state_changed;
    }

  (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);

  if (action == svn_wc_notify_patch)
    {
      int i;
      apr_pool_t *iterpool;
      apr_hash_index_t *hash_index;

      iterpool = svn_pool_create(pool);
      for (i = 0; i < target->content_info->hunks->nelts; i++)
        {
          const hunk_info_t *hi;

          svn_pool_clear(iterpool);

          hi = APR_ARRAY_IDX(target->content_info->hunks, i, hunk_info_t *);

          SVN_ERR(send_hunk_notification(hi, target, NULL /* prop_name */, 
                                         ctx, iterpool));
        }

      for (hash_index = apr_hash_first(pool, target->prop_targets);
           hash_index;
           hash_index = apr_hash_next(hash_index))
        {
          prop_patch_target_t *prop_target; 
          
          prop_target = svn__apr_hash_index_val(hash_index);

          for (i = 0; i < prop_target->content_info->hunks->nelts; i++)
            {
              const hunk_info_t *hi;

              svn_pool_clear(iterpool);

              hi = APR_ARRAY_IDX(prop_target->content_info->hunks, i,
                                 hunk_info_t *);

              /* Don't notify on the hunk level for added or deleted props. */
              if (prop_target->operation != svn_diff_op_added &&
                  prop_target->operation != svn_diff_op_deleted)
                SVN_ERR(send_hunk_notification(hi, target, prop_target->name, 
                                               ctx, iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Close the streams of the TARGET so that their content is flushed
 * to disk. This will also close underlying streams and files. Use POOL for
 * temporary allocations. */
static svn_error_t *
close_target_streams(const patch_target_t *target,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  target_content_info_t *prop_content_info;

   /* First the streams belonging to properties .. */
      for (hi = apr_hash_first(pool, target->prop_targets);
           hi;
           hi = apr_hash_next(hi))
        {
          prop_patch_target_t *prop_target;
          prop_target = svn__apr_hash_index_val(hi);
          prop_content_info = prop_target->content_info;

          /* ### If the prop did not exist pre-patching we'll not have a
           * ### stream to read from. Find a better way to store info on
           * ### the existence of the target prop. */
          if (prop_content_info->stream)
            SVN_ERR(svn_stream_close(prop_content_info->stream));

          SVN_ERR(svn_stream_close(prop_content_info->patched));
        }


   /* .. And then streams associted with the file. The reject stream is
    * shared between all target_content_info structures. */
  if (target->kind_on_disk == svn_node_file)
    SVN_ERR(svn_stream_close(target->content_info->stream));
  SVN_ERR(svn_stream_close(target->content_info->patched));
  SVN_ERR(svn_stream_close(target->content_info->reject));

  return SVN_NO_ERROR;
}

/* Apply a PATCH to a working copy at ABS_WC_PATH and put the result
 * into temporary files, to be installed in the working copy later.
 * Return information about the patch target in *PATCH_TARGET, allocated
 * in RESULT_POOL. Use WC_CTX as the working copy context.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * REMOVE_TEMPFILES, PATCH_FUNC, and PATCH_BATON as in svn_client_patch().
 * IGNORE_WHITESPACE tells whether whitespace should be considered when
 * doing the matching.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
apply_one_patch(patch_target_t **patch_target, svn_patch_t *patch,
                const char *abs_wc_path, svn_wc_context_t *wc_ctx,
                int strip_count,
                svn_boolean_t ignore_whitespace,
                svn_boolean_t remove_tempfiles,
                svn_client_patch_func_t patch_func,
                void *patch_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;
  apr_pool_t *iterpool;
  int i;
  static const int MAX_FUZZ = 2;
  apr_hash_index_t *hash_index;

  SVN_ERR(init_patch_target(&target, patch, abs_wc_path, wc_ctx, strip_count,
                            remove_tempfiles, result_pool, scratch_pool));
  if (target->skipped)
    {
      *patch_target = target;
      return SVN_NO_ERROR;
    }

  if (patch_func)
    {
      SVN_ERR(patch_func(patch_baton, &target->filtered,
                         target->canon_path_from_patchfile,
                         target->patched_path, target->reject_path,
                         scratch_pool));
      if (target->filtered)
        {
          *patch_target = target;
          return SVN_NO_ERROR;
        }
    }

  iterpool = svn_pool_create(scratch_pool);
  /* Match hunks. */
  for (i = 0; i < patch->hunks->nelts; i++)
    {
      svn_diff_hunk_t *hunk;
      hunk_info_t *hi;
      int fuzz = 0;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR((cancel_func)(cancel_baton));

      hunk = APR_ARRAY_IDX(patch->hunks, i, svn_diff_hunk_t *);

      /* Determine the line the hunk should be applied at.
       * If no match is found initially, try with fuzz. */
      do
        {
          SVN_ERR(get_hunk_info(&hi, target, target->content_info, hunk, fuzz,
                                ignore_whitespace,
                                FALSE /* is_prop_hunk */,
                                cancel_func, cancel_baton,
                                result_pool, iterpool));
          fuzz++;
        }
      while (hi->rejected && fuzz <= MAX_FUZZ && ! hi->already_applied);

      APR_ARRAY_PUSH(target->content_info->hunks, hunk_info_t *) = hi;
    }

  /* Apply or reject hunks. */
  for (i = 0; i < target->content_info->hunks->nelts; i++)
    {
      hunk_info_t *hi;

      svn_pool_clear(iterpool);

      hi = APR_ARRAY_IDX(target->content_info->hunks, i, hunk_info_t *);
      if (hi->already_applied)
        continue;
      else if (hi->rejected)
        SVN_ERR(reject_hunk(target, target->content_info, hi,
                            NULL /* prop_name */, 
                            iterpool));
      else
        SVN_ERR(apply_hunk(target, target->content_info, hi,
                           NULL /* prop_name */,  iterpool));
    }

  if (target->kind_on_disk == svn_node_file)
    {
      /* Copy any remaining lines to target. */
      SVN_ERR(copy_lines_to_target(target->content_info, 0,
                                   target->patched_path, scratch_pool));
      if (! target->content_info->eof)
        {
          /* We could not copy the entire target file to the temporary file,
           * and would truncate the target if we copied the temporary file
           * on top of it. Skip this target. */
          target->skipped = TRUE;
        }
    }

  /* Match property hunks.   ### Can we use scratch_pool here? */
  for (hash_index = apr_hash_first(result_pool, patch->prop_patches);
       hash_index;
       hash_index = apr_hash_next(hash_index))
    {
      svn_prop_patch_t *prop_patch;
      const char *prop_name;
      prop_patch_target_t *prop_target;
        
      prop_name = svn__apr_hash_index_key(hash_index);
      prop_patch = svn__apr_hash_index_val(hash_index);

      /* We'll store matched hunks in prop_content_info. */
      prop_target = apr_hash_get(target->prop_targets, prop_name, 
                                 APR_HASH_KEY_STRING);

      for (i = 0; i < prop_patch->hunks->nelts; i++)
        {
          svn_diff_hunk_t *hunk;
          hunk_info_t *hi;
          int fuzz = 0;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR((cancel_func)(cancel_baton));

          hunk = APR_ARRAY_IDX(prop_patch->hunks, i, svn_diff_hunk_t *);

          /* Determine the line the hunk should be applied at.
           * If no match is found initially, try with fuzz. */
          do
            {
              SVN_ERR(get_hunk_info(&hi, target, prop_target->content_info, 
                                    hunk, fuzz,
                                    ignore_whitespace,
                                    TRUE /* is_prop_hunk */,
                                    cancel_func, cancel_baton,
                                    result_pool, iterpool));
              fuzz++;
            }
          while (hi->rejected && fuzz <= MAX_FUZZ && ! hi->already_applied);

          APR_ARRAY_PUSH(prop_target->content_info->hunks, hunk_info_t *) = hi;
        }
    }

  /* Apply or reject property hunks. */
  for (hash_index = apr_hash_first(result_pool, target->prop_targets);
       hash_index;
       hash_index = apr_hash_next(hash_index))
    {
      prop_patch_target_t *prop_target;

      prop_target = svn__apr_hash_index_val(hash_index);

      for (i = 0; i < prop_target->content_info->hunks->nelts; i++)
        {
          hunk_info_t *hi;

          svn_pool_clear(iterpool);

          hi = APR_ARRAY_IDX(prop_target->content_info->hunks, i, 
                             hunk_info_t *);
          if (hi->already_applied)
            continue;
          else if (hi->rejected)
            SVN_ERR(reject_hunk(target, prop_target->content_info, hi,
                                prop_target->name,
                                iterpool));
          else
            SVN_ERR(apply_hunk(target, prop_target->content_info, hi, 
                               prop_target->name,
                               iterpool));
        }

        if (prop_target->content_info->stream)
          {
            /* Copy any remaining lines to target. */
            SVN_ERR(copy_lines_to_target(prop_target->content_info, 0,
                                         prop_target->patched_path, 
                                         scratch_pool));
            if (! prop_target->content_info->eof)
              {
                /* We could not copy the entire target property to the
                 * temporary file, and would truncate the target if we
                 * copied the temporary file on top of it. Skip this target.  */
                target->skipped = TRUE;
              }
          }
      }

  svn_pool_destroy(iterpool);

  SVN_ERR(close_target_streams(target, scratch_pool));

  if (! target->skipped)
    {
      apr_finfo_t working_file;
      apr_finfo_t patched_file;

      /* Get sizes of the patched temporary file and the working file.
       * We'll need those to figure out whether we should delete the
       * patched file. */
      SVN_ERR(svn_io_stat(&patched_file, target->patched_path,
                          APR_FINFO_SIZE, scratch_pool));
      if (target->kind_on_disk == svn_node_file)
        SVN_ERR(svn_io_stat(&working_file, target->local_abspath,
                            APR_FINFO_SIZE, scratch_pool));
      else
        working_file.size = 0;

      if (patched_file.size == 0 && working_file.size > 0)
        {
          /* If a unidiff removes all lines from a file, that usually
           * means deletion, so we can confidently schedule the target
           * for deletion. In the rare case where the unidiff was really
           * meant to replace a file with an empty one, this may not
           * be desirable. But the deletion can easily be reverted and
           * creating an empty file manually is not exactly hard either. */
          target->deleted = (target->db_kind == svn_node_file);
        }
      else if (patched_file.size == 0 && working_file.size == 0)
        {
          /* The target was empty or non-existent to begin with
           * and no content was changed by patching.
           * Report this as skipped if it didn't exist, unless in the special
           * case of adding an empty file which has properties set on it or
           * adding an empty file with a 'git diff' */
          if (target->kind_on_disk == svn_node_none 
              && ! target->has_prop_changes
              && ! target->added)
            target->skipped = TRUE;
        }
      else if (patched_file.size > 0 && working_file.size == 0)
        {
          /* The patch has created a file. */
          if (target->locally_deleted)
            target->replaced = TRUE;
          else if (target->db_kind == svn_node_none)
            target->added = TRUE;
        }
    }

  *patch_target = target;

  return SVN_NO_ERROR;
}

/* Try to create missing parent directories for TARGET in the working copy
 * rooted at ABS_WC_PATH, and add the parents to version control.
 * If the parents cannot be created, mark the target as skipped.
 * Use client context CTX. If DRY_RUN is true, do not create missing
 * parents but issue notifications only.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
create_missing_parents(patch_target_t *target,
                       const char *abs_wc_path,
                       svn_client_ctx_t *ctx,
                       svn_boolean_t dry_run,
                       apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  apr_array_header_t *components;
  int present_components;
  int i;
  apr_pool_t *iterpool;

  /* Check if we can safely create the target's parent. */
  local_abspath = abs_wc_path;
  components = svn_path_decompose(target->local_relpath, scratch_pool);
  present_components = 0;
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < components->nelts - 1; i++)
    {
      const char *component;
      svn_node_kind_t wc_kind, disk_kind;
      svn_boolean_t is_deleted;

      svn_pool_clear(iterpool);

      component = APR_ARRAY_IDX(components, i,
                                const char *);
      local_abspath = svn_dirent_join(local_abspath, component, scratch_pool);

      SVN_ERR(svn_wc_read_kind(&wc_kind, ctx->wc_ctx, local_abspath, TRUE,
                               iterpool));

      SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, iterpool));

      if (wc_kind != svn_node_none)
        SVN_ERR(svn_wc__node_is_status_deleted(&is_deleted,
                                               ctx->wc_ctx,
                                               local_abspath,
                                               iterpool));
      else
        is_deleted = FALSE;

      if (disk_kind == svn_node_file
          || (wc_kind == svn_node_file && !is_deleted))
        {
          /* on-disk files and missing files are obstructions */
          target->skipped = TRUE;
          break;
        }
      else if (wc_kind == svn_node_dir)
        {
          if (is_deleted)
            {
              target->skipped = TRUE;
              break;
            }

          /* continue one level deeper */
          present_components++;
        }
      else if (disk_kind == svn_node_dir)
        {
          /* Obstructed. ### BH: why? We can just add a directory */
          target->skipped = TRUE;
          break;
        }
      else
        {
          /* It's not a file, it's not a dir...
             Let's add a dir */
          break;
        }
    }

  if (! target->skipped)
    {
      local_abspath = abs_wc_path;
      for (i = 0; i < present_components; i++)
        {
          const char *component;
          component = APR_ARRAY_IDX(components, i,
                                    const char *);
          local_abspath = svn_dirent_join(local_abspath,
                                          component, scratch_pool);
        }

      if (!dry_run && present_components < components->nelts - 1)
        SVN_ERR(svn_io_make_dir_recursively(
                        svn_dirent_join(
                                   abs_wc_path,
                                   svn_relpath_dirname(target->local_relpath,
                                                       scratch_pool),
                                   scratch_pool),
                        scratch_pool));

      for (i = present_components; i < components->nelts - 1; i++)
        {
          const char *component;

          svn_pool_clear(iterpool);

          component = APR_ARRAY_IDX(components, i,
                                    const char *);
          local_abspath = svn_dirent_join(local_abspath, component,
                                     scratch_pool);
          if (dry_run)
            {
              if (ctx->notify_func2)
                {
                  /* Just do notification. */
                  svn_wc_notify_t *notify;
                  notify = svn_wc_create_notify(local_abspath,
                                                svn_wc_notify_add,
                                                iterpool);
                  notify->kind = svn_node_dir;
                  ctx->notify_func2(ctx->notify_baton2, notify,
                                    iterpool);
                }
            }
          else
            {
              /* Create the missing component and add it
               * to version control. Allow cancellation since we
               * have not modified the working copy yet for this
               * target. */
              SVN_ERR(svn_wc_add_from_disk(ctx->wc_ctx, local_abspath,
                                           ctx->cancel_func, ctx->cancel_baton,
                                           ctx->notify_func2, ctx->notify_baton2,
                                           iterpool));
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Install a patched TARGET into the working copy at ABS_WC_PATH.
 * Use client context CTX to retrieve WC_CTX, and possibly doing
 * notifications. If DRY_RUN is TRUE, don't modify the working copy.
 * Do temporary allocations in POOL. */
static svn_error_t *
install_patched_target(patch_target_t *target, const char *abs_wc_path,
                       svn_client_ctx_t *ctx, svn_boolean_t dry_run,
                       apr_pool_t *pool)
{
  if (target->deleted)
    {
      if (! dry_run)
        {
          /* Schedule the target for deletion.  Suppress
           * notification, we'll do it manually in a minute
           * because we also need to notify during dry-run.
           * Also suppress cancellation, because we'd rather
           * notify about what we did before aborting. */
          SVN_ERR(svn_wc_delete4(ctx->wc_ctx, target->local_abspath,
                                 FALSE /* keep_local */, FALSE,
                                 NULL, NULL, NULL, NULL, pool));
        }
    }
  else
    {
      svn_node_kind_t parent_db_kind;

      if (target->added)
        {
          /* If the target's parent directory does not yet exist
           * we need to create it before we can copy the patched
           * result in place. */
          SVN_ERR(svn_wc_read_kind(&parent_db_kind, ctx->wc_ctx,
                                   svn_dirent_dirname(target->local_abspath,
                                                      pool),
                                   FALSE, pool));

          /* We don't allow targets to be added under dirs scheduled for
           * deletion. */
          if (parent_db_kind == svn_node_dir)
            {
              const char *parent_abspath;
              svn_boolean_t is_deleted;
              
              parent_abspath = svn_dirent_dirname(target->local_abspath,
                                                  pool);
              SVN_ERR(svn_wc__node_is_status_deleted(&is_deleted, ctx->wc_ctx,
                                                     parent_abspath, pool));
              if (is_deleted)
                {
                  target->skipped = TRUE;
                  return SVN_NO_ERROR;
                }
            }
          else
            SVN_ERR(create_missing_parents(target, abs_wc_path, ctx,
                                           dry_run, pool));
        }

      if (! dry_run && ! target->skipped)
        {
          /* Copy the patched file on top of the target file. */
          SVN_ERR(svn_io_copy_file(target->patched_path,
                                   target->local_abspath, FALSE, pool));
          if (target->added || target->replaced)
            {
              /* The target file didn't exist previously,
               * so add it to version control.
               * Suppress notification, we'll do that later (and also
               * during dry-run). Also suppress cancellation because
               * we'd rather notify about what we did before aborting. */
              SVN_ERR(svn_wc_add_from_disk(ctx->wc_ctx, target->local_abspath,
                                           NULL, NULL, NULL, NULL, pool));
            }

          /* Restore the target's executable bit if necessary. */
          SVN_ERR(svn_io_set_file_executable(target->local_abspath,
                                             target->executable,
                                             FALSE, pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Write out rejected hunks, if any, to TARGET->REJECT_PATH. If DRY_RUN is
 * TRUE, don't modify the working copy.  
 * Do temporary allocations in POOL.
 */
static svn_error_t *
write_out_rejected_hunks(patch_target_t *target,
                         svn_boolean_t dry_run,
                         apr_pool_t *pool)
{
  if (! dry_run && (target->had_rejects || target->had_prop_rejects))
    {
      /* Write out rejected hunks, if any. */
      SVN_ERR(svn_io_copy_file(target->reject_path,
                               apr_psprintf(pool, "%s.svnpatch.rej",
                               target->local_abspath),
                               FALSE, pool));
      /* ### TODO mark file as conflicted. */
    }
  return SVN_NO_ERROR;
}

/* Install the patched properties for TARGET. Use client context CTX to
 * retrieve WC_CTX. If DRY_RUN is TRUE, don't modify the working copy.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
install_patched_prop_targets(patch_target_t *target,
                             svn_client_ctx_t *ctx, svn_boolean_t dry_run,
                             apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  if (dry_run)
    {
      if (! target->has_text_changes && target->kind_on_disk == svn_node_none)
        target->added = TRUE;

      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);

  for (hi = apr_hash_first(scratch_pool, target->prop_targets);
       hi;
       hi = apr_hash_next(hi))
    {
      prop_patch_target_t *prop_target = svn__apr_hash_index_val(hi); 
      apr_file_t *file;
      svn_stream_t *patched_stream;
      svn_stringbuf_t *line;
      svn_stringbuf_t *prop_content;
      const char *eol_str;
      svn_boolean_t eof;

      svn_pool_clear(iterpool);

      /* For a deleted prop we only set the value to NULL. */
      if (prop_target->operation == svn_diff_op_deleted)
        {
          SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, target->local_abspath,
                                   prop_target->name, NULL, 
                                   TRUE /* skip_checks */,
                                   NULL, NULL, /* suppress notification */
                                   iterpool));
          continue;
        }

      /* A property is usually small, at most a couple of bytes.
       * Start out assuming it won't be larger than a typical line of text. */
      prop_content = svn_stringbuf_create_ensure(80, scratch_pool);

      /* svn_wc_prop_set4() wants a svn_string_t for input so we need to
       * open the tmp file for reading again.
       * ### Just keep it open? */
      SVN_ERR(svn_io_file_open(&file, prop_target->patched_path,
                               APR_READ | APR_BINARY, APR_OS_DEFAULT,
                               scratch_pool));

      patched_stream = svn_stream_from_aprfile2(file, FALSE /* disown */,
                                                iterpool);
      do
        {
          SVN_ERR(svn_stream_readline_detect_eol(patched_stream,
                                                 &line, &eol_str,
                                                 &eof,
                                                 iterpool));

          svn_stringbuf_appendstr(prop_content, line);

          if (eol_str)
            svn_stringbuf_appendcstr(prop_content, eol_str);
        }
      while (! eof);

      SVN_ERR(svn_stream_close(patched_stream));

      /* If the patch target doesn't exist yet, the patch wants to add an
       * empty file with properties set on it. So create an empty file and
       * add it to version control. But if the patch was in the 'git format'
       * then the file has already been added.
       *
       * ### How can we tell whether the patch really wanted to create
       * ### an empty directory? */
      if (! target->has_text_changes 
          && target->kind_on_disk == svn_node_none
          && ! target->added)
        {
          SVN_ERR(svn_io_file_create(target->local_abspath, "", scratch_pool));
          SVN_ERR(svn_wc_add_from_disk(ctx->wc_ctx, target->local_abspath,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       NULL, NULL, /* suppress notification */
                                       iterpool));
          target->added = TRUE;
        }

      /* ### How should we handle SVN_ERR_ILLEGAL_TARGET and
       * ### SVN_ERR_BAD_MIME_TYPE?
       *
       * ### stsp: I'd say reject the property hunk.
       * ###       We should verify all modified prop hunk texts using
       * ###       svn_wc_canonicalize_svn_prop() before starting the
       * ###       patching process, to reject them as early as possible. */
      SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, target->local_abspath,
                               prop_target->name,
                               svn_string_create_from_buf(prop_content, 
                                                          iterpool),
                               TRUE /* skip_checks */,
                               NULL, NULL,
                               iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Baton for find_existing_children() */
struct status_baton
{
  apr_array_header_t *existing_targets;
  const char *parent_path;
  apr_pool_t *result_pool;
};

/* Implements svn_wc_status_func4_t. */
static svn_error_t *
find_existing_children(void *baton,
                       const char *abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *pool)
{
  struct status_baton *btn = baton;

  if (status->node_status != svn_wc_status_none
      && status->node_status != svn_wc_status_deleted
      && strcmp(abspath, btn->parent_path))
    {
      APR_ARRAY_PUSH(btn->existing_targets, 
                     const char *) = apr_pstrdup(btn->result_pool,
                                                 abspath);
    }

  return SVN_NO_ERROR;
}

/* Indicate in *EMPTY whether the directory at LOCAL_ABSPATH has any
 * versioned or unversioned children. Consider any DELETED_TARGETS,
 * as well as paths listed in DELETED_ABSPATHS_LIST (which may be NULL)
 * as already deleted. Use WC_CTX as the working copy context.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
check_dir_empty(svn_boolean_t *empty, const char *local_abspath,
                svn_wc_context_t *wc_ctx, 
                apr_array_header_t *deleted_targets,
                apr_array_header_t *deleted_abspath_list,
                apr_pool_t *scratch_pool)
{
  struct status_baton btn;
  svn_boolean_t is_wc_root;
  int i;

  /* Working copy root cannot be deleted, so never consider it empty. */
  SVN_ERR(svn_wc__strictly_is_wc_root(&is_wc_root, wc_ctx, local_abspath,
                                      scratch_pool));
  if (is_wc_root)
    {
      *empty = FALSE;
      return SVN_NO_ERROR;
    }

  /* Find existing children of the directory. */
  btn.existing_targets = apr_array_make(scratch_pool, 0, 
                                        sizeof(patch_target_t *));
  btn.parent_path = local_abspath;
  btn.result_pool = scratch_pool;
  SVN_ERR(svn_wc_walk_status(wc_ctx, local_abspath, svn_depth_immediates,
                             TRUE, TRUE, NULL, find_existing_children,
                             &btn, NULL, NULL, NULL, NULL, scratch_pool));
  *empty = TRUE;

  /* Do we delete all children? */
  for (i = 0; i < btn.existing_targets->nelts; i++)
    {
      int j;
      const char *found;
      svn_boolean_t deleted;

      deleted = FALSE;
      found = APR_ARRAY_IDX(btn.existing_targets, i, const char *);

      for (j = 0; j < deleted_targets->nelts; j++)
        {
          patch_target_info_t *target_info;

          target_info = APR_ARRAY_IDX(deleted_targets, j,
                                      patch_target_info_t *);
          if (! svn_path_compare_paths(found, target_info->local_abspath))
           {
              deleted = TRUE;
              break;
           }
        }
      if (! deleted && deleted_abspath_list)
        {
          for (j = 0; j < deleted_abspath_list->nelts; j++)
            {
              const char *abspath;

              abspath = APR_ARRAY_IDX(deleted_abspath_list, j, const char *);
              if (! svn_path_compare_paths(found, abspath))
               {
                  deleted = TRUE;
                  break;
               }
            }
        }
      if (! deleted)
        {
          *empty = FALSE;
          break;
        }
    }

  return SVN_NO_ERROR;
}

/* Push a copy of EMPTY_DIR, allocated in RESULT_POOL, onto the EMPTY_DIRS
 * array if no directory matching EMPTY_DIR is already in the array. */
static void
push_if_unique(apr_array_header_t *empty_dirs, const char *empty_dir,
               apr_pool_t *result_pool)
{
  svn_boolean_t is_unique;
  int i;

  is_unique = TRUE;
  for (i = 0; i < empty_dirs->nelts; i++)
    {
      const char *empty_dir2;

      empty_dir2 = APR_ARRAY_IDX(empty_dirs, i, const char *);
      if (strcmp(empty_dir, empty_dir2) == 0)
        {
          is_unique = FALSE;
          break;
        }
    }

  if (is_unique)
    APR_ARRAY_PUSH(empty_dirs, const char *) = apr_pstrdup(result_pool,
                                                           empty_dir);
}

/* Delete all directories from the working copy which are left empty
 * by deleted TARGETS. Use client context CTX.
 * If DRY_RUN is TRUE, do not modify the working copy.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
delete_empty_dirs(apr_array_header_t *targets_info, svn_client_ctx_t *ctx,
                  svn_boolean_t dry_run, apr_pool_t *scratch_pool)
{
  apr_array_header_t *empty_dirs;
  apr_array_header_t *deleted_targets;
  apr_pool_t *iterpool;
  svn_boolean_t again;
  int i;

  /* Get a list of all deleted targets. */
  deleted_targets = apr_array_make(scratch_pool, 0, sizeof(patch_target_t *));
  for (i = 0; i < targets_info->nelts; i++)
    {
      patch_target_info_t *target_info;

      target_info = APR_ARRAY_IDX(targets_info, i, patch_target_info_t *);
      if (target_info->deleted)
        APR_ARRAY_PUSH(deleted_targets, patch_target_info_t *) = target_info;
    }

  /* We have nothing to do if there aren't any deleted targets. */
  if (deleted_targets->nelts == 0)
    return SVN_NO_ERROR;

  /* Look for empty parent directories of deleted targets. */
  empty_dirs = apr_array_make(scratch_pool, 0, sizeof(const char *));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets_info->nelts; i++)
    {
      svn_boolean_t parent_empty;
      patch_target_info_t *target_info;
      const char *parent;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      target_info = APR_ARRAY_IDX(targets_info, i, patch_target_info_t *);
      parent = svn_dirent_dirname(target_info->local_abspath, iterpool);
      SVN_ERR(check_dir_empty(&parent_empty, parent, ctx->wc_ctx,
                              deleted_targets, NULL, iterpool));
      if (parent_empty)
        {
          APR_ARRAY_PUSH(empty_dirs, const char *) =
            apr_pstrdup(scratch_pool, parent);
        }
    }

  /* We have nothing to do if there aren't any empty directories. */
  if (empty_dirs->nelts == 0)
    {
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  /* Determine the minimal set of empty directories we need to delete. */
  do
    {
      apr_array_header_t *empty_dirs_copy;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Rebuild the empty dirs list, replacing empty dirs which have
       * an empty parent with their parent. */
      again = FALSE;
      empty_dirs_copy = apr_array_copy(iterpool, empty_dirs);
      apr_array_clear(empty_dirs);
      for (i = 0; i < empty_dirs_copy->nelts; i++)
        {
          svn_boolean_t parent_empty;
          const char *empty_dir;
          const char *parent;

          empty_dir = APR_ARRAY_IDX(empty_dirs_copy, i, const char *);
          parent = svn_dirent_dirname(empty_dir, iterpool);
          SVN_ERR(check_dir_empty(&parent_empty, parent, ctx->wc_ctx,
                                  deleted_targets, empty_dirs_copy,
                                  iterpool));
          if (parent_empty)
            {
              again = TRUE;
              push_if_unique(empty_dirs, parent, scratch_pool);
            }
          else
            push_if_unique(empty_dirs, empty_dir, scratch_pool);
        }
    }
  while (again);

  /* Finally, delete empty directories. */
  for (i = 0; i < empty_dirs->nelts; i++)
    {
      const char *empty_dir;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      empty_dir = APR_ARRAY_IDX(empty_dirs, i, const char *);
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify(empty_dir, svn_wc_notify_delete,
                                        iterpool);
          (*ctx->notify_func2)(ctx->notify_baton2, notify, iterpool);
        }
      if (! dry_run)
        SVN_ERR(svn_wc_delete4(ctx->wc_ctx, empty_dir, FALSE, FALSE,
                               ctx->cancel_func, ctx->cancel_baton,
                               NULL, NULL, /* no duplicate notification */
                               iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Baton for apply_patches(). */
typedef struct {
  /* The path to the patch file. */
  const char *patch_abspath;

  /* The abspath to the working copy the patch should be applied to. */
  const char *abs_wc_path;

  /* Indicates whether we're doing a dry run. */
  svn_boolean_t dry_run;

  /* Number of leading components to strip from patch target paths. */
  int strip_count;

  /* Whether to apply the patch in reverse. */
  svn_boolean_t reverse;

  /* Indicates whether we should ignore whitespace when matching context
   * lines */
  svn_boolean_t ignore_whitespace;

  /* As in svn_client_patch(). */
  svn_boolean_t remove_tempfiles;

  /* As in svn_client_patch(). */
  svn_client_patch_func_t patch_func;
  void *patch_baton;

  /* The client context. */
  svn_client_ctx_t *ctx;
} apply_patches_baton_t;

/* Callback for use with svn_wc__call_with_write_lock().
 * This function is the main entry point into the patch code. */
static svn_error_t *
apply_patches(void *baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_patch_t *patch;
  apr_pool_t *iterpool;
  const char *patch_eol_str;
  apr_file_t *patch_file;
  apr_array_header_t *targets_info;
  apply_patches_baton_t *btn = baton;

  /* Try to open the patch file. */
  SVN_ERR(svn_io_file_open(&patch_file, btn->patch_abspath,
                           APR_READ | APR_BINARY, 0, scratch_pool));

  SVN_ERR(svn_eol__detect_file_eol(&patch_eol_str, patch_file, scratch_pool));
  if (patch_eol_str == NULL)
    {
      /* If we can't figure out the EOL scheme, just assume native.
       * It's most likely a bad patch file anyway that will fail to
       * apply later. */
      patch_eol_str = APR_EOL_STR;
    }

  /* Apply patches. */
  targets_info = apr_array_make(scratch_pool, 0,
                                sizeof(patch_target_info_t *));
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      if (btn->ctx->cancel_func)
        SVN_ERR(btn->ctx->cancel_func(btn->ctx->cancel_baton));

      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file,
                                        btn->reverse, btn->ignore_whitespace,
                                        iterpool, iterpool));
      if (patch)
        {
          patch_target_t *target;

          SVN_ERR(apply_one_patch(&target, patch, btn->abs_wc_path,
                                  btn->ctx->wc_ctx, btn->strip_count,
                                  btn->ignore_whitespace,
                                  btn->remove_tempfiles,
                                  btn->patch_func, btn->patch_baton,
                                  btn->ctx->cancel_func,
                                  btn->ctx->cancel_baton,
                                  iterpool, iterpool));
          if (! target->filtered)
            {
              /* Save info we'll still need when we're done patching. */
              patch_target_info_t *target_info =
                apr_palloc(scratch_pool, sizeof(patch_target_info_t));
              target_info->local_abspath = apr_pstrdup(scratch_pool,
                                                       target->local_abspath);
              target_info->deleted = target->deleted;
              APR_ARRAY_PUSH(targets_info,
                             patch_target_info_t *) = target_info;

              if (! target->skipped)
                {
                  if (target->has_text_changes 
                      || target->added 
                      || target->deleted)
                    SVN_ERR(install_patched_target(target, btn->abs_wc_path,
                                                   btn->ctx, btn->dry_run,
                                                   iterpool));

                  if (target->has_prop_changes)
                    SVN_ERR(install_patched_prop_targets(target, btn->ctx, 
                                                         btn->dry_run,
                                                         iterpool));

                  SVN_ERR(write_out_rejected_hunks(target, btn->dry_run,
                                                   iterpool));
                }
              SVN_ERR(send_patch_notification(target, btn->ctx, iterpool));
            }

          SVN_ERR(svn_diff_close_patch(patch, iterpool));
        }
    }
  while (patch);

  /* Delete directories which are empty after patching, if any. */
  SVN_ERR(delete_empty_dirs(targets_info, btn->ctx, btn->dry_run,
                            scratch_pool));

  SVN_ERR(svn_io_file_close(patch_file, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_patch(const char *patch_abspath,
                 const char *local_abspath,
                 svn_boolean_t dry_run,
                 int strip_count,
                 svn_boolean_t reverse,
                 svn_boolean_t ignore_whitespace,
                 svn_boolean_t remove_tempfiles,
                 svn_client_patch_func_t patch_func,
                 void *patch_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apply_patches_baton_t baton;

  if (strip_count < 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("strip count must be positive"));

  baton.patch_abspath = patch_abspath;
  baton.abs_wc_path = local_abspath;
  baton.dry_run = dry_run;
  baton.ctx = ctx;
  baton.strip_count = strip_count;
  baton.reverse = reverse;
  baton.ignore_whitespace = ignore_whitespace;
  baton.remove_tempfiles = remove_tempfiles;
  baton.patch_func = patch_func;
  baton.patch_baton = patch_baton;

  return svn_error_return(
           svn_wc__call_with_write_lock(apply_patches, &baton,
                                        ctx->wc_ctx, local_abspath, FALSE,
                                        result_pool, scratch_pool));
}
