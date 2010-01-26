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
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_subst.h"
#include "svn_wc.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_diff_private.h"
#include "private/svn_eol_private.h"
#include "private/svn_wc_private.h"

typedef struct {
  /* The hunk. */
  const svn_hunk_t *hunk;

  /* The line where the hunk matched in the target file. */
  svn_linenum_t matched_line;
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

  /* The patched stream, without EOL transformation and keyword contraction. */
  svn_stream_t *patched_raw;

  /* Path to the temporary file underlying the result stream. */
  const char *patched_path;

  /* The reject stream, write-only, not seekable.
   * Hunks that are rejected will be written to this stream.
   * The data in the underlying file needs to be in repository-normal form,
   * so EOL transformation and keyword contraction is done transparently. */
  svn_stream_t *reject;

  /* The reject stream, without EOL transformation and keyword contraction. */
  svn_stream_t *reject_raw;

  /* Path to the temporary file underlying the reject stream. */
  const char *reject_path;

  /* The line last read from the target file. */
  svn_linenum_t current_line;

  /* EOL-marker used by target file. */
  const char *eol_str;

  /* An array containing stream markers marking the beginning
   * each line in the target stream. */
  apr_array_header_t *lines;

  /* An array containing hunk_info_t structures for hunks already matched. */
  apr_array_header_t *matched_hunks;

  /* The node kind of the target as found on disk prior
   * to patch application. */
  svn_node_kind_t kind;

  /* True if end-of-file was reached while reading from the target. */
  svn_boolean_t eof;

  /* True if the target had to be skipped for some reason. */
  svn_boolean_t skipped;

  /* True if at least one hunk was applied to the target.
   * The hunk may have been a no-op, however (e.g. a hunk trying
   * to delete a line from an empty file). */
  svn_boolean_t modified;

  /* True if at least one hunk was rejected. */
  svn_boolean_t had_rejects;

  /* True if the target file had local modifications before the
   * patch was applied to it. */
  svn_boolean_t local_mods;

  /* True if the target was added by the patch, which means that it
   * did not exist on disk before patching and does exist on disk
   * after patching. */
  svn_boolean_t added;

  /* True if the target ended up being deleted by the patch. */
  svn_boolean_t deleted;

  /* True if the target has the executable bit set. */
  svn_boolean_t executable;

  /* True if the target's parent directory exists. */
  svn_boolean_t parent_dir_exists;

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
  svn_wc_status2_t *status;

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
        target->added = FALSE;
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
          target->added = TRUE;
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

/* Indicate in *LOCAL_MODS whether the file at LOCAL_ABSPATH, has local
   modifications. */
static svn_error_t *
check_local_mods(svn_boolean_t *local_mods,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_text_modified_p2(local_mods, wc_ctx, local_abspath, FALSE,
                                pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
        {
          /* The target file is not versioned, that's OK.
           * We can treat it as unmodified. */
          svn_error_clear(err);
          *local_mods = FALSE;
        }
      else
        return svn_error_return(err);
    }

  return SVN_NO_ERROR;
}

/* Attempt to initialize a patch TARGET structure for a target file
 * described by PATCH.
 * Use client context CTX to send notifiations and retrieve WC_CTX.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * Upon success, allocate the patch target structure in RESULT_POOL.
 * Else, set *target to NULL.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
init_patch_target(patch_target_t **target,
                  const svn_patch_t *patch,
                  const char *base_dir,
                  const svn_client_ctx_t *ctx, int strip_count,
                  apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *new_target;

  *target = NULL;
  new_target = apr_pcalloc(result_pool, sizeof(*new_target));

  SVN_ERR(resolve_target_path(new_target, patch->new_filename,
                              base_dir, strip_count, ctx->wc_ctx,
                              result_pool, scratch_pool));

  if (new_target->kind == svn_node_file && ! new_target->skipped)
    {
      /* Try to open the target file */
      SVN_ERR(svn_io_file_open(&new_target->file, new_target->abs_path,
                               APR_READ | APR_BINARY | APR_BUFFERED,
                               APR_OS_DEFAULT, result_pool));
      SVN_ERR(svn_eol__detect_file_eol(&new_target->eol_str, new_target->file,
                                        scratch_pool));
      new_target->stream = svn_stream_from_aprfile2(new_target->file, FALSE,
                                                    result_pool);
    }

  if (new_target->eol_str == NULL)
    {
      /* Either we couldn't figure out the target files's EOL scheme,
       * or the target file doesn't exist. Just use native EOL makers. */
      new_target->eol_str = APR_EOL_STR;
    }

  new_target->local_mods = FALSE;
  new_target->executable = FALSE;

  if (! new_target->skipped)
    {
      apr_hash_t *keywords;
      const char *diff_header;
      apr_size_t len;

      /* TODO: Get keywords from patch target. How?
       * Can't call svn_wc__get_keywords() here? */
      keywords = apr_hash_make(result_pool);

      /* Create a temporary file to write the patched result to. */
      SVN_ERR(svn_stream_open_unique(&new_target->patched_raw,
                                     &new_target->patched_path, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     result_pool, scratch_pool));
      new_target->patched = svn_subst_stream_translated(new_target->patched_raw,
                                                        "\n", TRUE,
                                                        keywords, FALSE,
                                                        result_pool);

      SVN_ERR(check_local_mods(&new_target->local_mods, ctx->wc_ctx,
                               new_target->abs_path, scratch_pool));

      if (new_target->kind == svn_node_file)
        SVN_ERR(svn_io_is_file_executable(&new_target->executable,
                                          new_target->abs_path,
                                          scratch_pool));

      /* We'll also need a stream to write rejected hunks to. */
      SVN_ERR(svn_stream_open_unique(&new_target->reject_raw,
                                     &new_target->reject_path, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     result_pool, scratch_pool));
      new_target->reject = svn_subst_stream_translated(
                               new_target->reject_raw, "\n",
                               TRUE, keywords, FALSE, result_pool);
      diff_header = apr_psprintf(scratch_pool, "--- %s%s+++ %s%s",
                                 new_target->canon_path_from_patchfile,
                                 new_target->eol_str,
                                 new_target->canon_path_from_patchfile,
                                 new_target->eol_str);
      len = strlen(diff_header);
      SVN_ERR(svn_stream_write(new_target->reject, diff_header, &len));
    }

  new_target->patch = patch;
  new_target->current_line = 1;
  new_target->modified = FALSE;
  new_target->had_rejects = FALSE;
  new_target->deleted = FALSE;
  new_target->eof = FALSE;
  new_target->pool = result_pool;
  new_target->lines = apr_array_make(result_pool, 0,
                                     sizeof(svn_stream_mark_t *));
  new_target->matched_hunks = apr_array_make(result_pool, 0,
                                             sizeof(hunk_info_t *));
  *target = new_target;
  return SVN_NO_ERROR;
}

/* Read a line from TARGET. If this line has not been read before
 * mark the line in TARGET->LINES.
 * Return the line in *STRINGBUF, allocated in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
read_line(patch_target_t *target,
          svn_stringbuf_t **stringbuf,
          apr_pool_t *scratch_pool,
          apr_pool_t *result_pool)
{
  if (target->eof)
    {
      *stringbuf = svn_stringbuf_create_ensure(0, result_pool);
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(target->current_line <= target->lines->nelts + 1);
  if (target->current_line == target->lines->nelts + 1)
    {
      svn_stream_mark_t *mark;
      SVN_ERR(svn_stream_mark(target->stream, &mark, target->pool));
      APR_ARRAY_PUSH(target->lines, svn_stream_mark_t *) = mark;
    }

  SVN_ERR(svn_stream_readline(target->stream, stringbuf, target->eol_str,
                              &target->eof, result_pool));
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
  SVN_ERR_ASSERT(line > 0);

  if (line == target->current_line)
    return SVN_NO_ERROR;

  if (line <= target->lines->nelts)
    {
      svn_stream_mark_t *mark;

      mark = APR_ARRAY_IDX(target->lines, line - 1, svn_stream_mark_t *);
      SVN_ERR(svn_stream_seek(target->stream, mark));
      target->current_line = line;
    }
  else
    {
      svn_stringbuf_t *dummy;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      while (target->current_line < line)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(read_line(target, &dummy, iterpool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Indicate in *MATCHED whether the original text of HUNK matches
 * the patch TARGET at its current line.
 * When this function returns, neither TARGET->CURRENT_LINE nor the
 * file offset in the target file will have changed.
 * HUNK->ORIGINAL_TEXT will be reset. Do temporary allocations in POOL. */
static svn_error_t *
match_hunk(svn_boolean_t *matched, patch_target_t *target,
           const svn_hunk_t *hunk, apr_pool_t *pool)
{
  svn_stringbuf_t *hunk_line;
  svn_stringbuf_t *target_line;
  svn_linenum_t saved_line;
  svn_boolean_t hunk_eof;
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;

  *matched = FALSE;

  if (target->eof)
    return SVN_NO_ERROR;

  saved_line = target->current_line;
  lines_matched = FALSE;
  SVN_ERR(svn_stream_reset(hunk->original_text));
  iterpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(hunk->original_text, &hunk_line,
                                             NULL, &hunk_eof, iterpool));
      SVN_ERR(read_line(target, &target_line, iterpool, iterpool));
      if (! hunk_eof)
        {
          lines_matched = (hunk_line->len == target_line->len &&
                           ! strcmp(hunk_line->data, target_line->data));
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
  target->eof = FALSE;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Scan lines of TARGET for a match of the original text of HUNK,
 * up to but not including the specified UPPER_LINE.
 * If UPPER_LINE is zero scan until EOF occurs when reading from TARGET.
 * Return the line at which HUNK was matched in *MATCHED_LINE.
 * If the hunk did not match at all, set *MATCHED_LINE to zero.
 * If the hunk matched multiple times, and MATCH_FIRST is TRUE,
 * return the line number at which the first match occured in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is FALSE,
 * return the line number at which the last match occured in *MATCHED_LINE.
 * Do all allocations in POOL. */
static svn_error_t *
scan_for_match(svn_linenum_t *matched_line, patch_target_t *target,
               const svn_hunk_t *hunk, svn_boolean_t match_first,
               svn_linenum_t upper_line, apr_pool_t *pool)
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

      SVN_ERR(match_hunk(&matched, target, hunk, iterpool));
      if (matched)
        {
          svn_boolean_t taken = FALSE;

          /* Don't allow hunks to match at overlapping locations. */
          for (i = 0; i < target->matched_hunks->nelts; i++)
            {
              const hunk_info_t *hi;
              
              hi = APR_ARRAY_IDX(target->matched_hunks, i, hunk_info_t *);
              taken = (target->current_line >= hi->matched_line &&
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

      SVN_ERR(seek_to_line(target, target->current_line + 1, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Determine the line at which a HUNK applies to the TARGET file,
 * and return an appropriate hunk_info object in *HI, allocated from
 * RESULT_POOL. If no correct line can be determined,
 * set HI->MATCHED_LINE to zero.
 * When this function returns, neither TARGET->CURRENT_LINE nor the
 * file offset in the target file will have changed.
 * Do temporary allocations in POOL. */
static svn_error_t *
get_hunk_info(hunk_info_t **hi, patch_target_t *target,
              const svn_hunk_t *hunk, apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_linenum_t matched_line;

  /* An original offset of zero means that this hunk wants to create
   * a new file, potentially overwriting all content of an existing
   * file in the WC. Don't bother matching hunks in that case, since
   * the hunk applies at line 1. */
  matched_line = 1;
  if (hunk->original_start > 0 && target->kind == svn_node_file)
    {
      svn_linenum_t saved_line = target->current_line;
      svn_boolean_t saved_eof = target->eof;

      /* Scan for a match at the line where the hunk thinks it
       * should be going. */
      SVN_ERR(seek_to_line(target, hunk->original_start, scratch_pool));
      SVN_ERR(scan_for_match(&matched_line, target, hunk, TRUE,
                             hunk->original_start + 1, scratch_pool));
      if (matched_line != hunk->original_start)
        {
          /* Scan the whole file again from the start. */
          SVN_ERR(seek_to_line(target, 1, scratch_pool));

          /* Scan forward towards the hunk's line and look for a line
           * where the hunk matches. */
          SVN_ERR(scan_for_match(&matched_line, target, hunk, FALSE,
                                 hunk->original_start, scratch_pool));

          /* In tie-break situations, we arbitrarily prefer early matches
           * to save us from scanning the rest of the file. */
          if (matched_line == 0)
            {
              /* Scan forward towards the end of the file and look
               * for a line where the hunk matches. */
              SVN_ERR(scan_for_match(&matched_line, target, hunk, TRUE, 0,
                                     scratch_pool));
            }
        }

      SVN_ERR(seek_to_line(target, saved_line, scratch_pool));
      target->eof = saved_eof;
    }

  (*hi) = apr_palloc(result_pool, sizeof(hunk_info_t));
  (*hi)->matched_line = matched_line;
  (*hi)->hunk = hunk;

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
      svn_stringbuf_t *target_line;

      svn_pool_clear(iterpool);

      SVN_ERR(read_line(target, &target_line, iterpool, iterpool));
      if (! target->eof)
        svn_stringbuf_appendcstr(target_line, target->eol_str);

      SVN_ERR(try_stream_write(target->patched, target->patched_path,
                               target_line->data, target_line->len, pool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Copy HUNK_TEXT into TARGET, line by line, such that the line filter
 * and transformation callbacks set on HUNK_TEXT by the diff parsing
 * code in libsvn_diff will trigger. ABSPATH is the absolute path to the
 * file underlying TARGET. */
static svn_error_t *
copy_hunk_text(svn_stream_t *hunk_text, svn_stream_t *target,
               const char *abspath, apr_pool_t *scratch_pool)
{
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline_detect_eol(hunk_text, &line, &eol_str,
                                             &eof, iterpool));
      if (! eof)
        {
          if (line->len >= 1)
            SVN_ERR(try_stream_write(target, abspath, line->data, line->len,
                                     iterpool));
          if (eol_str)
            SVN_ERR(try_stream_write(target, abspath, eol_str, strlen(eol_str),
                                     iterpool));
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
reject_hunk(patch_target_t *target, const svn_hunk_t *hunk, apr_pool_t *pool)
{
  const char *hunk_header;
  apr_size_t len;

  hunk_header = apr_psprintf(pool, "@@ -%lu,%lu +%lu,%lu @@%s",
                             hunk->original_start, hunk->original_length,
                             hunk->modified_start, hunk->modified_length,
                             target->eol_str);
  len = strlen(hunk_header);
  SVN_ERR(svn_stream_write(target->reject, hunk_header, &len));

  SVN_ERR(copy_hunk_text(hunk->diff_text, target->reject,
                         target->reject_path, pool));

  target->had_rejects = TRUE;
  return SVN_NO_ERROR;
}

/* Apply a HUNK to a patch TARGET. Do all allocations in POOL. */
static svn_error_t *
apply_one_hunk(const hunk_info_t *hi, patch_target_t *target, apr_pool_t *pool)
{
  if (target->kind == svn_node_file)
    {
      /* Move forward to the hunk's line, copying data as we go. */
      SVN_ERR(copy_lines_to_target(target, hi->matched_line, pool));
      if (target->eof)
        {
          /* File is shorter than it should be. */
          SVN_ERR(reject_hunk(target, hi->hunk, pool));
          return SVN_NO_ERROR;
        }

      /* Skip the target's version of the hunk. */
      SVN_ERR(seek_to_line(target,
                           target->current_line + hi->hunk->original_length,
                           pool));
    }

  /* Copy the patched hunk text into the patched stream. */
  SVN_ERR(copy_hunk_text(hi->hunk->modified_text, target->patched,
                         target->patched_path, pool));

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for a patch TARGET.
 * Use POOL for temporary allocations. */
static svn_error_t *
maybe_send_patch_notification(const patch_target_t *target,
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
      if (target->kind == svn_node_none ||
          target->kind == svn_node_unknown)
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
      else if (target->modified)
        notify->content_state = svn_wc_notify_state_changed;
      else
        notify->content_state = svn_wc_notify_state_unchanged;
    }

  (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);

  return SVN_NO_ERROR;
}

/* Apply a PATCH to a working copy at ABS_WC_PATH.
 * Use client context CTX to send notifiations and retrieve WC_CTX.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * Do all allocations in POOL. */
static svn_error_t *
apply_one_patch(svn_patch_t *patch, const char *abs_wc_path,
                svn_boolean_t dry_run, svn_client_ctx_t *ctx,
                int strip_count, apr_pool_t *pool)
{
  patch_target_t *target;
  apr_pool_t *iterpool;
  apr_finfo_t working_file;
  apr_finfo_t patched_file;
  int i;

  SVN_ERR(init_patch_target(&target, patch, abs_wc_path, ctx, strip_count,
                            pool, pool));
  if (target == NULL)
    return SVN_NO_ERROR;

  if (target->skipped)
    {
      SVN_ERR(maybe_send_patch_notification(target, ctx, pool));
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(pool);
  /* Match hunks. */
  for (i = 0; i < patch->hunks->nelts; i++)
    {
      svn_hunk_t *hunk;
      hunk_info_t *hi;

      svn_pool_clear(iterpool);

      hunk = APR_ARRAY_IDX(patch->hunks, i, svn_hunk_t *);

      /* Determine the line the hunk should be applied at. */
      SVN_ERR(get_hunk_info(&hi, target, hunk, pool, iterpool));
      if (hi->matched_line == 0)
        {
          /* The hunk does not apply, reject it. */
          SVN_ERR(reject_hunk(target, hunk, iterpool));
        }
      else
        APR_ARRAY_PUSH(target->matched_hunks, hunk_info_t *) = hi;
    }

  /* Apply hunks. */
  for (i = 0; i < target->matched_hunks->nelts; i++)
    {
      const hunk_info_t *hi;

      svn_pool_clear(iterpool);

      hi = APR_ARRAY_IDX(target->matched_hunks, i, const hunk_info_t *);
      SVN_ERR(apply_one_hunk(hi, target, iterpool));
    }
  svn_pool_destroy(iterpool);

  if (target->kind == svn_node_file)
    {
      /* Copy any remaining lines to target. */
      SVN_ERR(copy_lines_to_target(target, 0, pool));
      if (! target->eof)
        {
          /* We could not copy the entire target file to the temporary file,
           * and would truncate the target if we copied the temporary file
           * on top of it. Cancel any modifications to the target file and
           * report is as skipped. */
          target->modified = FALSE;
          target->skipped = TRUE;
        }

      /* Closing this stream will also close the underlying file. */
      SVN_ERR(svn_stream_close(target->stream));
    }

  /* Close the patched and reject streams so that their content is
   * flushed to disk. This will also close the raw streams. */
  SVN_ERR(svn_stream_close(target->patched));
  SVN_ERR(svn_stream_close(target->reject));

  /* Get sizes of the patched temporary file and the working file.
   * We'll need those to figure out whether we should add or delete
   * the patched file. */
  SVN_ERR(svn_io_stat(&patched_file, target->patched_path, APR_FINFO_SIZE,
                      pool));
  if (target->kind == svn_node_file)
    SVN_ERR(svn_io_stat(&working_file, target->abs_path, APR_FINFO_SIZE,
                        pool));
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
      target->deleted = (target->kind != svn_node_none);
    }

  if (target->deleted)
    {
      if (! dry_run)
        {
          svn_wc_adm_access_t *adm_access;

          /* Schedule the target for deletion.  Suppress
           * notification, we'll do it manually in a minute. */

          /* ### svn_wc_delete4() still needs an adm access */
          SVN_ERR(svn_wc__adm_open_in_context(&adm_access, ctx->wc_ctx,
                                  svn_dirent_dirname(target->abs_path,
                                                     pool),
                                  TRUE, -1, ctx->cancel_func,
                                  ctx->cancel_baton, pool));

          SVN_ERR(svn_wc_delete4(ctx->wc_ctx, target->abs_path,
                                 FALSE /* keep_local */, FALSE,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 NULL, NULL, pool));

          SVN_ERR(svn_wc_adm_close2(adm_access, pool));
        }
    }
  else
    {
      if (working_file.size == 0 && patched_file.size == 0)
        {
          /* The target was empty or non-existent to begin with
           * and nothing has changed by patching.
           * Report this as skipped if it didn't exist. */
          if (target->kind != svn_node_file)
            target->skipped = TRUE;
        }
      else
        {
          target->modified = TRUE;

          /* If the target's parent directory does not yet exist
           * we need to create it before we can copy the patched
           * result in place. */
          if (target->added && ! target->parent_dir_exists)
            {
              if (dry_run)
                {
                  /* During a dry run, we need to check if we
                   * could create the target's parent directory
                   * if we wanted to. */
                  const char *abs_path;
                  apr_array_header_t *components;
                  int missing_components;

                  abs_path = apr_pstrdup(pool, abs_wc_path);
                  components = svn_path_decompose(target->rel_path, pool);
                  missing_components = 0;
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
                          /* A normal run would be obstructed. */
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
                              /* A normal run would schedule conflict. */
                              target->skipped = TRUE;
                              break;
                            }
                        }

                      missing_components++;
                    }

                  /* Do notification for missing parent directories. */
                  if (! target->skipped && ctx->notify_func2)
                    {
                      abs_path = abs_wc_path;
                      for (i = 0; i < missing_components; i++)
                        {
                          svn_wc_notify_t *notify;
                          const char *component;

                          svn_pool_clear(iterpool);

                          component = APR_ARRAY_IDX(components, i,
                                                    const char *);
                          abs_path = svn_dirent_join(abs_path, component,
                                                     pool);
                          notify = svn_wc_create_notify(abs_path,
                                                        svn_wc_notify_add,
                                                        iterpool);
                          notify->kind = svn_node_dir;
                          ctx->notify_func2(ctx->notify_baton2, notify,
                                            iterpool);
                        }
                    }

                  svn_pool_destroy(iterpool);
                }
              else
                {
                  const char *dir_abspath;
                  svn_error_t *err;

                  dir_abspath = svn_dirent_dirname(target->abs_path, pool);

                  err = svn_client__make_local_parents(dir_abspath,
                                                       TRUE, ctx, 
                                                       pool);
                  /* ### wc-ng should eventually be able to replace
                   * directories in-place, so the schedule conflict
                   * will go away. */
                  if (err &&
                      (APR_STATUS_IS_ENOTDIR(err->apr_err) ||
                       err->apr_err == SVN_ERR_WC_SCHEDULE_CONFLICT))
                    {
                      /* Either an obstruction is blocking the creation of
                       * the target's parent directory, or the parent has
                       * been scheduled for deletion. Skip this target. */
                      target->skipped = TRUE;
                      svn_error_clear(err);
                    }
                  else
                    SVN_ERR(err);
                }
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

    }
  /* Write out rejected hunks, if any. */
  if (! target->skipped && target->had_rejects)
    {
      SVN_ERR(svn_io_copy_file(target->reject_path,
                               apr_psprintf(pool, "%s.svnpatch.rej",
                                            target->abs_path),
                               FALSE, pool));
      /* ### TODO mark file as conflicted. */
    }

  SVN_ERR(maybe_send_patch_notification(target, ctx, pool));

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

  /* The client context. */
  svn_client_ctx_t *ctx;

  /* Number of leading components to strip from patch target paths. */
  int strip_count;
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
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      if (btn->ctx->cancel_func)
        SVN_ERR(btn->ctx->cancel_func(btn->ctx->cancel_baton));

      SVN_ERR(svn_diff__parse_next_patch(&patch, patch_file, iterpool,
                                         iterpool));
      if (patch)
        {
          SVN_ERR(apply_one_patch(patch, btn->abs_wc_path, btn->dry_run,
                                  btn->ctx, btn->strip_count, iterpool));
          SVN_ERR(svn_diff__close_patch(patch));
        }
    }
  while (patch);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_patch(const char *abs_patch_path,
                 const char *local_abspath,
                 svn_boolean_t dry_run,
                 int strip_count,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
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

  SVN_ERR(svn_wc__call_with_write_lock(apply_patches, &baton,
                                       ctx->wc_ctx, local_abspath,
                                       pool, pool));

  return SVN_NO_ERROR;
}
