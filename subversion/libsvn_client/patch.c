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
#include "svn_subst.h"
#include "svn_wc.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_eol_private.h"
#include "private/svn_wc_private.h"

typedef struct {
  /* The hunk. */
  const svn_hunk_t *hunk;

  /* The line where the hunk matched in the target file. */
  svn_linenum_t matched_line;

  /* Whether this hunk has been rejected. */
  svn_boolean_t rejected;

  /* The fuzz factor used when matching this hunk, i.e. how many
   * lines of leading and trailing context to ignore during matching. */
  int fuzz;
} hunk_info_t;

typedef struct {
  /* The patch being applied. */
  const svn_patch_t *patch;

  /* The target path as it appeared in the patch file,
   * but in canonicalised form. */
  const char *canon_path_from_patchfile;

  /* The target path, relative to the working copy directory the
   * patch is being applied to. A patch strip count applies to this
   * and only this path. This is never NULL. */
  const char *rel_path;

  /* The absolute path of the target on the filesystem.
   * Any symlinks the path from the patch file may contain are resolved.
   * Is not always known, so it may be NULL. */
  char *abs_path;

  /* The target file, read-only, seekable. This is NULL in case the target
   * file did not exist prior to patch application. */
  apr_file_t *file;

  /* A stream to read lines form the target file. This is NULL in case
   * the target file did not exist prior to patch application. */
  svn_stream_t *stream;

  /* The patched stream, write-only, not seekable.
   * This stream is equivalent to the target, except that in appropriate
   * places it contains the modified text as it appears in the patch file.
   * The data in the underlying file needs to be in repository-normal form,
   * so EOL transformation and keyword contraction is done transparently. */
  svn_stream_t *patched;

  /* The patched stream, without EOL transformation and keyword expansion. */
  svn_stream_t *patched_raw;

  /* Path to the temporary file underlying the result stream. */
  const char *patched_path;

  /* The reject stream, write-only, not seekable.
   * Hunks that are rejected will be written to this stream. */
  svn_stream_t *reject;

  /* Path to the temporary file underlying the reject stream. */
  const char *reject_path;

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

  /* The node kind of the target as found on disk prior
   * to patch application. */
  svn_node_kind_t kind;

  /* True if end-of-file was reached while reading from the target. */
  svn_boolean_t eof;

  /* True if the target had to be skipped for some reason. */
  svn_boolean_t skipped;

  /* True if the target matches a filter glob pattern. */
  svn_boolean_t filtered;

  /* True if at least one hunk was rejected. */
  svn_boolean_t had_rejects;

  /* True if the target file had local modifications before the
   * patch was applied to it. */
  svn_boolean_t local_mods;

  /* True if the target was added by the patch, which means that it did
   * not exist on disk before patching and has content after patching. */
  svn_boolean_t added;

  /* True if the target ended up being deleted by the patch. */
  svn_boolean_t deleted;

  /* True if the target has the executable bit set. */
  svn_boolean_t executable;

  /* True if the target's parent directory exists. */
  svn_boolean_t parent_dir_exists;

  /* The keywords of the target. */
  apr_hash_t *keywords;

  /* The pool the target is allocated in. */
  apr_pool_t *pool;
} patch_target_t;


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
 * Use RESULT_POOL for allocations of fields in TARGET. */
static svn_error_t *
resolve_target_path(patch_target_t *target,
                    const char *path_from_patchfile,
                    const char *abs_wc_path,
                    int strip_count,
                    svn_wc_context_t *wc_ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *stripped_path;
  svn_wc_status3_t *status;

  target->canon_path_from_patchfile = svn_dirent_internal_style(
                                        path_from_patchfile, result_pool);
  if (target->canon_path_from_patchfile[0] == '\0')
    {
      /* An empty patch target path? What gives? Skip this. */
      target->skipped = TRUE;
      target->kind = svn_node_file;
      target->abs_path = NULL;
      target->rel_path = "";
      return SVN_NO_ERROR;
    }

  if (strip_count > 0)
    SVN_ERR(strip_path(&stripped_path, target->canon_path_from_patchfile,
                       strip_count, result_pool, scratch_pool));
  else
    stripped_path = target->canon_path_from_patchfile;

  if (svn_dirent_is_absolute(stripped_path))
    {
      target->rel_path = svn_dirent_is_child(abs_wc_path, stripped_path,
                                             result_pool);
      if (! target->rel_path)
        {
          /* The target path is either outside of the working copy
           * or it is the working copy itself. Skip it. */
          target->skipped = TRUE;
          target->kind = svn_node_file;
          target->abs_path = NULL;
          target->rel_path = stripped_path;
          return SVN_NO_ERROR;
        }
    }
  else
    {
      target->rel_path = stripped_path;
    }

  /* Make sure the path is secure to use. We want the target to be inside
   * of the working copy and not be fooled by symlinks it might contain. */
  if (! svn_dirent_is_under_root(&target->abs_path, abs_wc_path,
                                 target->rel_path, result_pool))
    {
      /* The target path is outside of the working copy. Skip it. */
      target->skipped = TRUE;
      target->kind = svn_node_file;
      target->abs_path = NULL;
      return SVN_NO_ERROR;
    }

  /* Skip things we should not be messing with. */
  SVN_ERR(svn_wc_status3(&status, wc_ctx, target->abs_path,
                         scratch_pool, scratch_pool));
  if (status->text_status == svn_wc_status_unversioned ||
      status->text_status == svn_wc_status_ignored ||
      status->text_status == svn_wc_status_obstructed)
    {
      target->skipped = TRUE;
      SVN_ERR(svn_io_check_path(target->abs_path, &target->kind,
                                scratch_pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__node_get_kind(&target->kind, wc_ctx, target->abs_path,
                                FALSE, scratch_pool));
  switch (target->kind)
    {
      case svn_node_file:
        target->parent_dir_exists = TRUE;
        break;
      case svn_node_none:
      case svn_node_unknown:
        {
          const char *abs_dirname;
          svn_node_kind_t kind;

          /* The file is not there, that's fine. The patch might want to
           * create it. Check if the containing directory of the target
           * exists. We may need to create it later. */
          abs_dirname = svn_dirent_dirname(target->abs_path, scratch_pool);
          SVN_ERR(svn_wc__node_get_kind(&kind, wc_ctx, abs_dirname,
                                        FALSE, scratch_pool));
          SVN_ERR(svn_wc_status3(&status, wc_ctx, abs_dirname,
                                 scratch_pool, scratch_pool));
          target->parent_dir_exists =
            (kind == svn_node_dir &&
             status->text_status != svn_wc_status_deleted &&
             status->text_status != svn_wc_status_missing);
          break;
        }
      default:
        target->skipped = TRUE;
        break;
    }

  return SVN_NO_ERROR;
}

/* Attempt to initialize a *PATCH_TARGET structure for a target file
 * described by PATCH. Use working copy context WC_CTX.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * Upon success, allocate the patch target structure in RESULT_POOL.
 * Else, set *target to NULL.
 * If a target does not match a glob in INCLUDE_PATTERNS, mark it as filtered.
 * If a target matches a glob in EXCLUDE_PATTERNS, mark it as filtered.
 * If PATCHED_TEMPFILES or REJECT_TEMPFILES are not NULL, add the path
 * to temporary patched/reject files to them, keyed by the target's path
 * as parsed from the patch file (after canonicalization).
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
init_patch_target(patch_target_t **patch_target,
                  const svn_patch_t *patch,
                  const char *base_dir,
                  svn_wc_context_t *wc_ctx, int strip_count,
                  const apr_array_header_t *include_patterns,
                  const apr_array_header_t *exclude_patterns,
                  apr_hash_t *patched_tempfiles,
                  apr_hash_t *reject_tempfiles,
                  apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;

  target = apr_pcalloc(result_pool, sizeof(*target));

  SVN_ERR(resolve_target_path(target, patch->new_filename,
                              base_dir, strip_count, wc_ctx,
                              result_pool, scratch_pool));

  target->filtered = FALSE;
  if (include_patterns)
    {
      int i;
      const char *glob;
      svn_boolean_t match;

      match = FALSE;
      for (i = 0; i < include_patterns->nelts; i++)
        {
          glob = APR_ARRAY_IDX(include_patterns, i, const char *);
          match = (apr_fnmatch(glob, target->canon_path_from_patchfile,
                               APR_FNM_CASE_BLIND) == APR_SUCCESS);
          if (match)
            break;
        }

      if (! match)
        {
          target->filtered = TRUE;
          *patch_target = target;
          return SVN_NO_ERROR;
        }
    }
  if (exclude_patterns)
    {
      int i;
      const char *glob;

      for (i = 0; i < exclude_patterns->nelts; i++)
        {
          glob = APR_ARRAY_IDX(exclude_patterns, i, const char *);
          target->filtered = (apr_fnmatch(glob,
                                          target->canon_path_from_patchfile,
                                          APR_FNM_CASE_BLIND) == APR_SUCCESS);
          if (target->filtered)
            {
              *patch_target = target;
              return SVN_NO_ERROR;
            }
        }
    }

  target->local_mods = FALSE;
  target->executable = FALSE;

  if (! target->skipped)
    {
      apr_hash_t *props;
      svn_string_t *keywords_val;
      svn_string_t *eol_style_val;
      const char *diff_header;
      svn_boolean_t repair_eol;
      apr_size_t len;

      target->keywords = NULL;
      target->eol_style = svn_subst_eol_style_none;
      target->eol_str = NULL;

      if (target->kind == svn_node_file)
        {
          /* Open the file. */
          SVN_ERR(svn_io_file_open(&target->file, target->abs_path,
                                   APR_READ | APR_BINARY | APR_BUFFERED,
                                   APR_OS_DEFAULT, result_pool));

          /* Handle svn:keyword and svn:eol-style properties. */
          SVN_ERR(svn_wc_prop_list2(&props, wc_ctx, target->abs_path,
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
                                                    target->abs_path,
                                                    scratch_pool,
                                                    scratch_pool));
              rev_str = apr_psprintf(scratch_pool, "%"SVN_REVNUM_T_FMT,
                                     changed_rev);
              SVN_ERR(svn_wc__node_get_url(&url, wc_ctx,
                                           target->abs_path,
                                           scratch_pool, scratch_pool));
              SVN_ERR(svn_subst_build_keywords2(&target->keywords,
                                                keywords_val->data,
                                                rev_str, url, changed_date,
                                                author, result_pool));
            }

          eol_style_val = apr_hash_get(props, SVN_PROP_EOL_STYLE,
                                       APR_HASH_KEY_STRING);
          if (eol_style_val)
            {
              svn_subst_eol_style_from_value(&target->eol_style,
                                             &target->eol_str,
                                             eol_style_val->data);
            }

          /* Create a stream to read from the target. */
          target->stream = svn_stream_from_aprfile2(target->file,
                                                    FALSE, result_pool);

          /* Also check the file for local mods and the Xbit. */
          SVN_ERR(svn_wc_text_modified_p2(&target->local_mods, wc_ctx,
                                          target->abs_path, FALSE,
                                          scratch_pool));
          SVN_ERR(svn_io_is_file_executable(&target->executable,
                                            target->abs_path,
                                            scratch_pool));
        }

      /* Create a temporary file to write the patched result to. */
      SVN_ERR(svn_stream_open_unique(&target->patched_raw,
                                     &target->patched_path, NULL,
                                     patched_tempfiles ?
                                       svn_io_file_del_none :
                                       svn_io_file_del_on_pool_cleanup,
                                     result_pool, scratch_pool));
      if (patched_tempfiles)
        apr_hash_set(patched_tempfiles, target->canon_path_from_patchfile,
                     APR_HASH_KEY_STRING, target->patched_path);

      /* Expand keywords in the patched file.
       * Repair newlines if svn:eol-style dictates a particular style. */
      repair_eol = (target->eol_style == svn_subst_eol_style_fixed ||
                    target->eol_style == svn_subst_eol_style_native);
      target->patched = svn_subst_stream_translated(
                              target->patched_raw, target->eol_str, repair_eol,
                              target->keywords, TRUE, result_pool);

      /* We'll also need a stream to write rejected hunks to.
       * We don't expand keywords, nor normalise line-endings,
       * in reject files. */
      SVN_ERR(svn_stream_open_unique(&target->reject,
                                     &target->reject_path, NULL,
                                     reject_tempfiles ?
                                       svn_io_file_del_none :
                                       svn_io_file_del_on_pool_cleanup,
                                     result_pool, scratch_pool));
      if (reject_tempfiles)
        apr_hash_set(reject_tempfiles, target->canon_path_from_patchfile,
                     APR_HASH_KEY_STRING, target->reject_path);

      /* The reject stream needs a diff header. */
      diff_header = apr_psprintf(scratch_pool, "--- %s%s+++ %s%s",
                                 target->canon_path_from_patchfile,
                                 APR_EOL_STR,
                                 target->canon_path_from_patchfile,
                                 APR_EOL_STR);
      len = strlen(diff_header);
      SVN_ERR(svn_stream_write(target->reject, diff_header, &len));
    }

  target->patch = patch;
  target->current_line = 1;
  target->had_rejects = FALSE;
  target->deleted = FALSE;
  target->eof = FALSE;
  target->pool = result_pool;
  target->lines = apr_array_make(result_pool, 0,
                                 sizeof(svn_stream_mark_t *));
  target->hunks = apr_array_make(result_pool, 0, sizeof(hunk_info_t *));

  *patch_target = target;
  return SVN_NO_ERROR;
}

/* Read a *LINE from TARGET. If the line has not been read before
 * mark the line in TARGET->LINES. Allocate *LINE in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
read_line(patch_target_t *target,
          const char **line,
          apr_pool_t *scratch_pool,
          apr_pool_t *result_pool)
{
  svn_stringbuf_t *line_raw;
  const char *eol_str;

  if (target->eof)
    {
      *line = "";
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(target->current_line <= target->lines->nelts + 1);
  if (target->current_line == target->lines->nelts + 1)
    {
      svn_stream_mark_t *mark;
      SVN_ERR(svn_stream_mark(target->stream, &mark, target->pool));
      APR_ARRAY_PUSH(target->lines, svn_stream_mark_t *) = mark;
    }

  SVN_ERR(svn_stream_readline_detect_eol(target->stream, &line_raw,
                                         &eol_str, &target->eof,
                                         scratch_pool));
  if (target->eol_style == svn_subst_eol_style_none)
    target->eol_str = eol_str;

  /* Contract keywords. */
  SVN_ERR(svn_subst_translate_cstring2(line_raw->data, line,
                                       NULL, FALSE,
                                       target->keywords, FALSE,
                                       result_pool));
  if (! target->eof)
    target->current_line++;

  return SVN_NO_ERROR;
}

/* Seek to the specified LINE in TARGET.
 * Mark any lines not read before in TARGET->LINES.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
seek_to_line(patch_target_t *target, svn_linenum_t line,
             apr_pool_t *scratch_pool)
{
  svn_linenum_t saved_line;
  svn_boolean_t saved_eof;

  SVN_ERR_ASSERT(line > 0);

  if (line == target->current_line)
    return SVN_NO_ERROR;

  saved_line = target->current_line;
  saved_eof = target->eof;

  if (line <= target->lines->nelts)
    {
      svn_stream_mark_t *mark;

      mark = APR_ARRAY_IDX(target->lines, line - 1, svn_stream_mark_t *);
      SVN_ERR(svn_stream_seek(target->stream, mark));
      target->current_line = line;
    }
  else
    {
      const char *dummy;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      while (! target->eof && target->current_line < line)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(read_line(target, &dummy, iterpool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  /* After seeking backwards from EOF position clear EOF indicator. */
  if (saved_eof && saved_line > target->current_line)
    target->eof = FALSE;

  return SVN_NO_ERROR;
}

/* Indicate in *MATCHED whether the original text of HUNK matches the patch
 * TARGET at its current line. Lines within FUZZ lines of the start or end
 * of HUNK will always match. If IGNORE_WHiTESPACES is set, we ignore
 * whitespaces when doing the matching. When this function returns, neither
 * TARGET->CURRENT_LINE nor the file offset in the target file will have
 * changed. HUNK->ORIGINAL_TEXT will be reset.  Do temporary allocations in
 * POOL. */
static svn_error_t *
match_hunk(svn_boolean_t *matched, patch_target_t *target,
           const svn_hunk_t *hunk, int fuzz, 
           svn_boolean_t ignore_whitespaces, apr_pool_t *pool)
{
  svn_stringbuf_t *hunk_line;
  const char *target_line;
  svn_linenum_t lines_read;
  svn_linenum_t saved_line;
  svn_boolean_t hunk_eof;
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;

  *matched = FALSE;

  if (target->eof)
    return SVN_NO_ERROR;

  saved_line = target->current_line;
  lines_read = 0;
  lines_matched = FALSE;
  SVN_ERR(svn_stream_reset(hunk->original_text));
  iterpool = svn_pool_create(pool);
  do
    {
      const char *hunk_line_translated;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(hunk->original_text,
                                             &hunk_line, NULL,
                                             &hunk_eof, iterpool));
      /* Contract keywords, if any, before matching. */
      SVN_ERR(svn_subst_translate_cstring2(hunk_line->data,
                                           &hunk_line_translated,
                                           NULL, FALSE,
                                           target->keywords, FALSE,
                                           iterpool));
      lines_read++;
      SVN_ERR(read_line(target, &target_line, iterpool, iterpool));
      if (! hunk_eof)
        {
          if (lines_read <= fuzz && hunk->leading_context > fuzz)
            lines_matched = TRUE;
          else if (lines_read > hunk->original_length - fuzz &&
                   hunk->trailing_context > fuzz)
            lines_matched = TRUE;
          else
            {
              if (ignore_whitespaces)
                {
                  char *stripped_hunk_line = apr_pstrdup(pool,
                                                         hunk_line_translated);
                  char *stripped_target_line = apr_pstrdup(pool, target_line);

                  apr_collapse_spaces(stripped_hunk_line,
                                      hunk_line_translated);
                  apr_collapse_spaces(stripped_target_line, target_line);
                  lines_matched = ! strcmp(stripped_hunk_line,
                                           stripped_target_line);
                }
              else 
                lines_matched = ! strcmp(hunk_line_translated, target_line);
            }
        }
    }
  while (lines_matched && ! (hunk_eof || target->eof));

  if (hunk_eof)
    *matched = lines_matched;
  else if (target->eof)
    {
      /* If the target has no newline at end-of-file, we get an EOF
       * indication for the target earlier than we do get it for the hunk. */
      SVN_ERR(svn_stream_readline_detect_eol(hunk->original_text, &hunk_line,
                                             NULL, &hunk_eof, iterpool));
      if (hunk_line->len == 0 && hunk_eof)
        *matched = lines_matched;
      else
        *matched = FALSE;
    }
  SVN_ERR(seek_to_line(target, saved_line, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Scan lines of TARGET for a match of the original text of HUNK,
 * up to but not including the specified UPPER_LINE. Use fuzz factor FUZZ.
 * If UPPER_LINE is zero scan until EOF occurs when reading from TARGET.
 * Return the line at which HUNK was matched in *MATCHED_LINE.
 * If the hunk did not match at all, set *MATCHED_LINE to zero.
 * If the hunk matched multiple times, and MATCH_FIRST is TRUE,
 * return the line number at which the first match occured in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is FALSE,
 * return the line number at which the last match occured in *MATCHED_LINE.
 * If IGNORE_WHiTESPACES is set, ignore whitespaces during the matching.
 * Do all allocations in POOL. */
static svn_error_t *
scan_for_match(svn_linenum_t *matched_line, patch_target_t *target,
               const svn_hunk_t *hunk, svn_boolean_t match_first,
               svn_linenum_t upper_line, int fuzz, 
               svn_boolean_t ignore_whitespaces,
               apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  *matched_line = 0;
  iterpool = svn_pool_create(pool);
  while ((target->current_line < upper_line || upper_line == 0) &&
         ! target->eof)
    {
      svn_boolean_t matched;
      int i;

      svn_pool_clear(iterpool);

      SVN_ERR(match_hunk(&matched, target, hunk, fuzz, ignore_whitespaces,
                         iterpool));
      if (matched)
        {
          svn_boolean_t taken = FALSE;

          /* Don't allow hunks to match at overlapping locations. */
          for (i = 0; i < target->hunks->nelts; i++)
            {
              const hunk_info_t *hi;

              hi = APR_ARRAY_IDX(target->hunks, i, const hunk_info_t *);
              taken = (! hi->rejected &&
                       target->current_line >= hi->matched_line &&
                       target->current_line < hi->matched_line +
                                              hi->hunk->original_length);
              if (taken)
                break;
            }

          if (! taken)
            {
              *matched_line = target->current_line;
              if (match_first)
                break;
            }
        }

      if (! target->eof)
        SVN_ERR(seek_to_line(target, target->current_line + 1, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Determine the line at which a HUNK applies to the TARGET file,
 * and return an appropriate hunk_info object in *HI, allocated from
 * RESULT_POOL. Use fuzz factor FUZZ. Set HI->FUZZ to FUZZ. If no correct
 * line can be determined, set HI->REJECTED to TRUE.
 * IGNORE_WHiTESPACES tells whether whitespaces should be considered when
 * matching. When this function returns, neither TARGET->CURRENT_LINE nor
 * the file offset in the target file will have changed.
 * Do temporary allocations in POOL. */
static svn_error_t *
get_hunk_info(hunk_info_t **hi, patch_target_t *target,
              const svn_hunk_t *hunk, int fuzz, 
              svn_boolean_t ignore_whitespaces, apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_linenum_t matched_line;

  /* An original offset of zero means that this hunk wants to create
   * a new file. Don't bother matching hunks in that case, since
   * the hunk applies at line 1. If the file already exists, the hunk
   * is rejected. */
  if (hunk->original_start == 0)
    {
      if (target->kind == svn_node_file)
        matched_line = 0;
      else
        matched_line = 1;
    }
  else if (hunk->original_start > 0 && target->kind == svn_node_file)
    {
      svn_linenum_t saved_line = target->current_line;

      /* Scan for a match at the line where the hunk thinks it
       * should be going. */
      SVN_ERR(seek_to_line(target, hunk->original_start, scratch_pool));
      if (target->current_line != hunk->original_start)
        {
          /* Seek failed. */
          matched_line = 0;
        }
      else
        SVN_ERR(scan_for_match(&matched_line, target, hunk, TRUE,
                               hunk->original_start + 1, fuzz,
                               ignore_whitespaces, scratch_pool));

      if (matched_line != hunk->original_start)
        {
          /* Scan the whole file again from the start. */
          SVN_ERR(seek_to_line(target, 1, scratch_pool));

          /* Scan forward towards the hunk's line and look for a line
           * where the hunk matches. */
          SVN_ERR(scan_for_match(&matched_line, target, hunk, FALSE,
                                 hunk->original_start, fuzz,
                                 ignore_whitespaces, scratch_pool));

          /* In tie-break situations, we arbitrarily prefer early matches
           * to save us from scanning the rest of the file. */
          if (matched_line == 0)
            {
              /* Scan forward towards the end of the file and look
               * for a line where the hunk matches. */
              SVN_ERR(scan_for_match(&matched_line, target, hunk, TRUE, 0,
                                     fuzz, ignore_whitespaces,
                                     scratch_pool));
            }
        }

      SVN_ERR(seek_to_line(target, saved_line, scratch_pool));
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
  (*hi)->fuzz = fuzz;

  return SVN_NO_ERROR;
}

/* Attempt to write LEN bytes of DATA to STREAM, the underlying file
 * of which is at ABSPATH. Fail if not all bytes could be written to
 * the stream. Do temporary allocations in POOL. */
static svn_error_t *
try_stream_write(svn_stream_t *stream, const char *abspath,
                 const char *data, apr_size_t len, apr_pool_t *pool)
{
  apr_size_t written;

  written = len;
  SVN_ERR(svn_stream_write(stream, data, &written));
  if (written != len)
    return svn_error_createf(SVN_ERR_IO_WRITE_ERROR, NULL,
                             _("Error writing to '%s'"),
                             svn_dirent_local_style(abspath, pool));
  return SVN_NO_ERROR;
}

/* Copy lines to the patched stream until the specified LINE has been
 * reached. Indicate in *EOF whether end-of-file was encountered while
 * reading from the target.
 * If LINE is zero, copy lines until end-of-file has been reached.
 * Do all allocations in POOL. */
static svn_error_t *
copy_lines_to_target(patch_target_t *target, svn_linenum_t line,
                     apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(pool);
  while ((target->current_line < line || line == 0) && ! target->eof)
    {
      const char *target_line;

      svn_pool_clear(iterpool);

      SVN_ERR(read_line(target, &target_line, iterpool, iterpool));
      if (! target->eof)
        target_line = apr_pstrcat(iterpool, target_line, target->eol_str, NULL);

      SVN_ERR(try_stream_write(target->patched, target->patched_path,
                               target_line, strlen(target_line), iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Write the diff text of the hunk described by HI to the
 * reject stream of TARGET, and mark TARGET as having had rejects.
 * Do temporary allocations in POOL. */
static svn_error_t *
reject_hunk(patch_target_t *target, hunk_info_t *hi, apr_pool_t *pool)
{
  const char *hunk_header;
  apr_size_t len;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  hunk_header = apr_psprintf(pool, "@@ -%lu,%lu +%lu,%lu @@%s",
                             hi->hunk->original_start,
                             hi->hunk->original_length,
                             hi->hunk->modified_start,
                             hi->hunk->modified_length,
                             APR_EOL_STR);
  len = strlen(hunk_header);
  SVN_ERR(svn_stream_write(target->reject, hunk_header, &len));

  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(hi->hunk->diff_text, &hunk_line,
                                             &eol_str, &eof, iterpool));
      if (! eof)
        {
          if (hunk_line->len >= 1)
            SVN_ERR(try_stream_write(target->reject, target->reject_path,
                                     hunk_line->data, hunk_line->len,
                                     iterpool));
          if (eol_str)
            SVN_ERR(try_stream_write(target->reject, target->reject_path,
                                     eol_str, strlen(eol_str), iterpool));
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  target->had_rejects = TRUE;

  return SVN_NO_ERROR;
}

/* Write the modified text of hunk described by HI to the patched
 * stream of TARGET. Do temporary allocations in POOL. */
static svn_error_t *
apply_hunk(patch_target_t *target, hunk_info_t *hi, apr_pool_t *pool)
{
  svn_linenum_t lines_read;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  if (target->kind == svn_node_file)
    {
      svn_linenum_t line;

      /* Move forward to the hunk's line, copying data as we go.
       * Also copy leading lines of context which matched with fuzz.
       * The target has changed on the fuzzy-matched lines,
       * so we should retain the target's version of those lines. */
      SVN_ERR(copy_lines_to_target(target, hi->matched_line + hi->fuzz,
                                   pool));

      /* Skip the target's version of the hunk.
       * Don't skip trailing lines which matched with fuzz. */
      line = target->current_line + hi->hunk->original_length - (2 * hi->fuzz);
      SVN_ERR(seek_to_line(target, line, pool));
      if (target->current_line != line)
        {
          /* Seek failed, reject this hunk. */
          hi->rejected = TRUE;
          SVN_ERR(reject_hunk(target, hi, pool));
          return SVN_NO_ERROR;
        }
    }

  /* Write the hunk's version to the patched result.
   * Don't write the lines which matched with fuzz. */
  lines_read = 0;
  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(hi->hunk->modified_text,
                                             &hunk_line, &eol_str,
                                             &eof, iterpool));
      lines_read++;
      if (! eof && lines_read > hi->fuzz &&
          lines_read <= hi->hunk->modified_length - hi->fuzz)
        {
          if (hunk_line->len >= 1)
            SVN_ERR(try_stream_write(target->patched, target->patched_path,
                                     hunk_line->data, hunk_line->len,
                                     iterpool));
          if (eol_str)
            {
              /* Use the EOL as it was read from the patch file,
               * unless the target's EOL style is set by svn:eol-style */
              if (target->eol_style != svn_subst_eol_style_none)
                eol_str = target->eol_str;

              SVN_ERR(try_stream_write(target->patched, target->patched_path,
                                       eol_str, strlen(eol_str), iterpool));
            }
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

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
  else if (target->added)
    action = svn_wc_notify_add;
  else
    action = svn_wc_notify_patch;

  notify = svn_wc_create_notify(target->abs_path ? target->abs_path
                                                 : target->rel_path,
                                action, pool);
  notify->kind = svn_node_file;

  if (action == svn_wc_notify_skip)
    {
      if (target->parent_dir_exists &&
          (target->kind == svn_node_none || target->kind == svn_node_unknown))
        notify->content_state = svn_wc_notify_state_missing;
      else if (target->kind == svn_node_dir)
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
      else
        notify->content_state = svn_wc_notify_state_changed;
    }

  (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);

  if (action == svn_wc_notify_patch)
    {
      int i;
      apr_pool_t *iterpool;

      iterpool = svn_pool_create(pool);
      for (i = 0; i < target->hunks->nelts; i++)
        {
          hunk_info_t *hi;

          svn_pool_clear(iterpool);

          hi = APR_ARRAY_IDX(target->hunks, i, hunk_info_t *);

          if (hi->rejected)
            action = svn_wc_notify_patch_rejected_hunk;
          else
            action = svn_wc_notify_patch_applied_hunk;

          notify = svn_wc_create_notify(target->abs_path ? target->abs_path
                                                         : target->rel_path,
                                        action, pool);
          notify->hunk_original_start = hi->hunk->original_start;
          notify->hunk_original_length = hi->hunk->original_length;
          notify->hunk_modified_start = hi->hunk->modified_start;
          notify->hunk_modified_length = hi->hunk->modified_length;
          notify->hunk_matched_line = hi->matched_line;
          notify->hunk_fuzz = hi->fuzz;

          (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Apply a PATCH to a working copy at ABS_WC_PATH and put the result
 * into temporary files, to be installed in the working copy later.
 * Return information about the patch target in *PATCH_TARGET, allocated
 * in RESULT_POOL. Use WC_CTX as the working copy context.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * If a target does not match a glob in INCLUDE_PATTERNS, mark it as filtered.
 * If a target matches a glob in EXCLUDE_PATTERNS, mark it as filtered.
 * If PATCHED_TEMPFILES or REJECT_TEMPFILES are not NULL, add the path
 * to temporary patched/reject files to them, keyed by the target's path
 * as parsed from the patch file (after canonicalization).
 * IGNORE_WHiTESPACES tells whether whitespaces should be considered when
 * doing the matching.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
apply_one_patch(patch_target_t **patch_target, svn_patch_t *patch,
                const char *abs_wc_path, svn_wc_context_t *wc_ctx,
                int strip_count,
                const apr_array_header_t *include_patterns,
                const apr_array_header_t *exclude_patterns,
                apr_hash_t *patched_tempfiles,
                apr_hash_t *reject_tempfiles,
                svn_boolean_t ignore_whitespaces,
                apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;
  apr_pool_t *iterpool;
  int i;
  static const int MAX_FUZZ = 2;

  SVN_ERR(init_patch_target(&target, patch, abs_wc_path, wc_ctx, strip_count,
                            include_patterns, exclude_patterns,
                            patched_tempfiles, reject_tempfiles,
                            result_pool, scratch_pool));

  if (target->skipped || target->filtered)
    {
      *patch_target = target;
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);
  /* Match hunks. */
  for (i = 0; i < patch->hunks->nelts; i++)
    {
      svn_hunk_t *hunk;
      hunk_info_t *hi;
      int fuzz = 0;

      svn_pool_clear(iterpool);

      hunk = APR_ARRAY_IDX(patch->hunks, i, svn_hunk_t *);

      /* Determine the line the hunk should be applied at.
       * If no match is found initially, try with fuzz. */
      do
        {
          SVN_ERR(get_hunk_info(&hi, target, hunk, fuzz,
                                ignore_whitespaces, result_pool, iterpool));
          fuzz++;
        }
      while (hi->rejected && fuzz <= MAX_FUZZ);

      APR_ARRAY_PUSH(target->hunks, hunk_info_t *) = hi;
    }

  /* Apply or reject hunks. */
  for (i = 0; i < target->hunks->nelts; i++)
    {
      hunk_info_t *hi;

      svn_pool_clear(iterpool);

      hi = APR_ARRAY_IDX(target->hunks, i, hunk_info_t *);
      if (hi->rejected)
        SVN_ERR(reject_hunk(target, hi, iterpool));
      else
        SVN_ERR(apply_hunk(target, hi, iterpool));
    }
  svn_pool_destroy(iterpool);

  if (target->kind == svn_node_file)
    {
      /* Copy any remaining lines to target. */
      SVN_ERR(copy_lines_to_target(target, 0, scratch_pool));
      if (! target->eof)
        {
          /* We could not copy the entire target file to the temporary file,
           * and would truncate the target if we copied the temporary file
           * on top of it. Skip this target. */
          target->skipped = TRUE;
        }
    }

  /* Close the streams of the target so that their content is
   * flushed to disk. This will also close underlying streams. */
  if (target->kind == svn_node_file)
    SVN_ERR(svn_stream_close(target->stream));
  SVN_ERR(svn_stream_close(target->patched));
  SVN_ERR(svn_stream_close(target->reject));

  if (! target->skipped)
    {
      apr_finfo_t working_file;
      apr_finfo_t patched_file;

      /* Get sizes of the patched temporary file and the working file.
       * We'll need those to figure out whether we should delete the
       * patched file. */
      SVN_ERR(svn_io_stat(&patched_file, target->patched_path,
                          APR_FINFO_SIZE, scratch_pool));
      if (target->kind == svn_node_file)
        SVN_ERR(svn_io_stat(&working_file, target->abs_path,
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
          target->deleted = (target->kind == svn_node_file);
        }
      else if (working_file.size == 0 && patched_file.size == 0)
        {
          /* The target was empty or non-existent to begin with
           * and nothing has changed by patching.
           * Report this as skipped if it didn't exist. */
          if (target->kind != svn_node_file)
            target->skipped = TRUE;
        }
      else if (target->kind != svn_node_file && patched_file.size > 0)
        {
          /* The patch has created a file. */
          target->added = TRUE;
        }
    }

  *patch_target = target;
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
           * notification, we'll do it manually in a minute.
           * Also suppress cancellation. */
          SVN_ERR(svn_wc_delete4(ctx->wc_ctx, target->abs_path,
                                 FALSE /* keep_local */, FALSE,
                                 NULL, NULL, NULL, NULL, pool));
        }
    }
  else
    {
      /* If the target's parent directory does not yet exist
       * we need to create it before we can copy the patched
       * result in place. */
      if (target->added && ! target->parent_dir_exists)
        {
          const char *abs_path;
          apr_array_header_t *components;
          int present_components;
          int i;
          apr_pool_t *iterpool;

          /* Check if we can safely create the target's parent. */
          abs_path = apr_pstrdup(pool, abs_wc_path);
          components = svn_path_decompose(target->rel_path, pool);
          present_components = 0;
          iterpool = svn_pool_create(pool);
          for (i = 0; i < components->nelts - 1; i++)
            {
              const char *component;
              svn_node_kind_t kind;

              svn_pool_clear(iterpool);

              component = APR_ARRAY_IDX(components, i,
                                        const char *);
              abs_path = svn_dirent_join(abs_path, component, pool);

              SVN_ERR(svn_wc__node_get_kind(&kind, ctx->wc_ctx,
                                            abs_path, TRUE,
                                            iterpool));
              if (kind == svn_node_file)
                {
                  /* Obstructed. */
                  target->skipped = TRUE;
                  break;
                }
              else if (kind == svn_node_dir)
                {
                  /* ### wc-ng should eventually be able to replace
                   * directories in-place, so this schedule conflict
                   * check will go away. We could then also make the
                   * svn_wc__node_get_kind() call above ignore hidden
                   * nodes.*/
                  svn_boolean_t is_deleted;

                  SVN_ERR(svn_wc__node_is_status_deleted(&is_deleted,
                                                         ctx->wc_ctx,
                                                         abs_path,
                                                         iterpool));
                  if (is_deleted)
                    {
                      target->skipped = TRUE;
                      break;
                    }

                  present_components++;
                }
              else
                {
                  /* The WC_DB doesn't know much about this node.
                   * Check what's on disk. */
                  svn_node_kind_t disk_kind;

                  SVN_ERR(svn_io_check_path(abs_path, &disk_kind, iterpool));
                  if (disk_kind != svn_node_none)
                    {
                      /* An unversioned item is in the way. */
                      target->skipped = TRUE;
                      break;
                    }
                }
            }

          if (! target->skipped)
            {
              abs_path = abs_wc_path;
              for (i = present_components; i < components->nelts - 1; i++)
                {
                  const char *component;

                  svn_pool_clear(iterpool);

                  component = APR_ARRAY_IDX(components, i,
                                            const char *);
                  abs_path = svn_dirent_join(abs_path, component,
                                             pool);
                  if (dry_run)
                    {
                      if (ctx->notify_func2)
                        {
                          /* Just do notification. */
                          svn_wc_notify_t *notify;
                          notify = svn_wc_create_notify(abs_path,
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
                       * to version control. Suppress cancellation. */
                      SVN_ERR(svn_io_dir_make(abs_path, APR_OS_DEFAULT,
                                              iterpool));
                      SVN_ERR(svn_wc_add4(ctx->wc_ctx, abs_path,
                                          svn_depth_infinity,
                                          NULL, SVN_INVALID_REVNUM,
                                          NULL, NULL,
                                          ctx->notify_func2,
                                          ctx->notify_baton2,
                                          iterpool));
                    }
                }
            }
          svn_pool_destroy(iterpool);
        }

      if (! dry_run && ! target->skipped)
        {
          /* Copy the patched file on top of the target file. */
          SVN_ERR(svn_io_copy_file(target->patched_path,
                                   target->abs_path, FALSE, pool));
          if (target->added)
            {
              /* The target file didn't exist previously,
               * so add it to version control.
               * Suppress notification, we'll do that later.
               * Also suppress cancellation. */
              SVN_ERR(svn_wc_add4(ctx->wc_ctx, target->abs_path,
                                  svn_depth_infinity,
                                  NULL, SVN_INVALID_REVNUM,
                                  NULL, NULL, NULL, NULL, pool));
            }

          /* Restore the target's executable bit if necessary. */
          SVN_ERR(svn_io_set_file_executable(target->abs_path,
                                             target->executable,
                                             FALSE, pool));
        }
    }

  /* Write out rejected hunks, if any. */
  if (! dry_run && ! target->skipped && target->had_rejects)
    {
      SVN_ERR(svn_io_copy_file(target->reject_path,
                               apr_psprintf(pool, "%s.svnpatch.rej",
                                            target->abs_path),
                               FALSE, pool));
      /* ### TODO mark file as conflicted. */
    }

  return SVN_NO_ERROR;
}

/* Baton for apply_patches(). */
typedef struct {
  /* The path to the patch file. */
  const char *abs_patch_path;

  /* The abspath to the working copy the patch should be applied to. */
  const char *abs_wc_path;

  /* Indicates whether we're doing a dry run. */
  svn_boolean_t dry_run;

  /* Number of leading components to strip from patch target paths. */
  int strip_count;

  /* Whether to apply the patch in reverse. */
  svn_boolean_t reverse;

  /* Files not matching any of these patterns won't be patched. */
  const apr_array_header_t *include_patterns;

  /* Files matching any of these patterns won't be patched. */
  const apr_array_header_t *exclude_patterns;

  /* Mapping patch target path -> path to tempfile with patched result. */
  apr_hash_t *patched_tempfiles;

  /* Mapping patch target path -> path to tempfile with rejected hunks. */
  apr_hash_t *reject_tempfiles;

  /* Indicates whether we should ignore whitespaces when matching context
   * lines */
  svn_boolean_t ignore_whitespaces;


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
  apr_array_header_t *targets;
  int i;
  apply_patches_baton_t *btn;

  btn = (apply_patches_baton_t *)baton;

  /* Try to open the patch file. */
  SVN_ERR(svn_io_file_open(&patch_file, btn->abs_patch_path,
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
  targets = apr_array_make(scratch_pool, 0, sizeof(patch_target_t *));
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      if (btn->ctx->cancel_func)
        SVN_ERR(btn->ctx->cancel_func(btn->ctx->cancel_baton));

      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file,
                                        btn->reverse, btn->ignore_whitespaces,
                                        scratch_pool, iterpool));
      if (patch)
        {
          patch_target_t *target;

          SVN_ERR(apply_one_patch(&target, patch, btn->abs_wc_path,
                                  btn->ctx->wc_ctx, btn->strip_count,
                                  btn->include_patterns, btn->exclude_patterns,
                                  btn->patched_tempfiles, btn->reject_tempfiles,
                                  btn->ignore_whitespaces,
                                  result_pool, iterpool));
          if (target->filtered)
            SVN_ERR(svn_diff_close_patch(patch));
          else
            APR_ARRAY_PUSH(targets, patch_target_t *) = target;
        }
    }
  while (patch);

  /* Install patched targets into the working copy. */
  for (i = 0; i < targets->nelts; i++)
    {
      patch_target_t *target;

      svn_pool_clear(iterpool);

      if (btn->ctx->cancel_func)
        SVN_ERR(btn->ctx->cancel_func(btn->ctx->cancel_baton));

      target = APR_ARRAY_IDX(targets, i, patch_target_t *);
      if (! target->skipped)
        SVN_ERR(install_patched_target(target, btn->abs_wc_path,
                                       btn->ctx, btn->dry_run, iterpool));
      SVN_ERR(send_patch_notification(target, btn->ctx, iterpool));
      SVN_ERR(svn_diff_close_patch(target->patch));
    }

  SVN_ERR(svn_io_file_close(patch_file, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_patch(const char *abs_patch_path,
                 const char *local_abspath,
                 svn_boolean_t dry_run,
                 int strip_count,
                 svn_boolean_t reverse,
                 const apr_array_header_t *include_patterns,
                 const apr_array_header_t *exclude_patterns,
                 apr_hash_t **patched_tempfiles,
                 apr_hash_t **reject_tempfiles,
                 svn_boolean_t ignore_whitespaces,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apply_patches_baton_t baton;

  if (strip_count < 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("strip count must be positive"));

  baton.abs_patch_path = abs_patch_path;
  baton.abs_wc_path = local_abspath;
  baton.dry_run = dry_run;
  baton.ctx = ctx;
  baton.strip_count = strip_count;
  baton.reverse = reverse;
  baton.include_patterns = include_patterns;
  baton.exclude_patterns = exclude_patterns;
  baton.ignore_whitespaces = ignore_whitespaces;

 if (patched_tempfiles)
    {
      (*patched_tempfiles) = apr_hash_make(result_pool);
      baton.patched_tempfiles = (*patched_tempfiles);
    }
  else
    baton.patched_tempfiles = NULL;
  if (reject_tempfiles)
    {
      (*reject_tempfiles) = apr_hash_make(result_pool);
      baton.reject_tempfiles = (*reject_tempfiles);
    }
  else
    baton.reject_tempfiles = NULL;

  SVN_ERR(svn_wc__call_with_write_lock(apply_patches, &baton,
                                       ctx->wc_ctx, local_abspath,
                                       result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
