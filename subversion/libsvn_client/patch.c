/*
 * patch.c:  wrapper around wc patch functionality.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "client.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_base64.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_subst.h"
#include "svn_hash.h"
#include <assert.h>

#include "svn_private_config.h"
#include "private/svn_diff_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_eol_private.h"

/* ### forward-declaration */
static svn_error_t *
apply_textdiffs(const char *patch_path, const char *wc_path,
                svn_wc_adm_access_t *adm_access, svn_boolean_t dry_run,
                const svn_client_ctx_t *ctx, apr_pool_t *pool);

svn_error_t *
svn_client_patch(const char *patch_path,
                 const char *target,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *abs_target;

  SVN_ERR(svn_dirent_get_absolute(&abs_target, target, pool));
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, abs_target,
                           TRUE, -1, ctx->cancel_func, ctx->cancel_baton,
                           pool));

  SVN_ERR(apply_textdiffs(patch_path, target, adm_access, dry_run, ctx, pool));

  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  return SVN_NO_ERROR;
}

/* --- Text-diff application routines --- */

typedef struct {
  /* ### Ideally, the diff API would allow us to diff the original,
   * modified and latest streams directly. But this is currently
   * not possible, so instead we're dumping the streams into temporary
   * files for diffing and merging. */
  apr_file_t *orig_file;
  apr_file_t *mod_file;
  apr_file_t *latest_file;
  /* On top of that, the diff API also wants filenames... */
  const char *orig_path;
  const char *mod_path;
  const char *latest_path;
} hunk_tempfiles_t;

typedef struct {
  /* The patch being applied. */
  const svn_patch_t *patch;

  /* The target path as it appeared in the patch file,
   * but in canonicalised form. */
  const char *canon_path_from_patchfile;

  /* The target path, relative to the working copy directory the
   * patch is being applied to. A patch strip count applies to this
   * and only this path. Is not always known, so may be NULL. */
  const char *wc_path;

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

  /* The result stream, write-only, not seekable.
   * This is where we write the patched result to. */
  svn_stream_t *result;

  /* Path to the temporary file underlying the result stream. */
  const char *result_path;

  /* The line last read from the target file. */
  svn_linenum_t current_line;

  /* EOL-marker used by target file. */
  const char *eol_str;

  /* Temporary files for hunk merging. */
  const hunk_tempfiles_t *tempfiles;

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

  /* True if at least one hunk application resulted in a conflict. */
  svn_boolean_t conflicted;

  /* True if the target file had local modifications before the
   * patch was applied to it. */
  svn_boolean_t local_mods;

  /* True if the target was added by the patch, which means that it
   * did not exist on disk before patching and does exist on disk
   * after patching. */
  svn_boolean_t added;

  /* True if the target ended up being deleted by the patch. */
  svn_boolean_t deleted;
} patch_target_t;

/* Resolve the exact path for a patch TARGET at path PATH_FROM_PATCHFILE,
 * which is the path of the target as it appeared in the patch file.
 * Put a canonicalized version of PATH_FROM_PATCHFILE into
 * TARGET->CANON_PATH_FROM_PATCHFILE.
 * If possible, determine TARGET->WC_PATH, TARGET->ABS_PATH, and TARGET->KIND.
 * Indicate in TARGET->SKIPPED whether the target should be skipped.
 * Use RESULT_POOL for allocations of fields in TARGET. */
static svn_error_t *
resolve_target_path(patch_target_t *target, const char *path_from_patchfile,
                    const char *wc_path, apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *abs_wc_path;

  target->canon_path_from_patchfile = svn_dirent_canonicalize(
                                        path_from_patchfile, result_pool);
  if (target->canon_path_from_patchfile[0] == '\0')
    {
      /* An empty patch target path? What gives? Skip this. */
      target->skipped = TRUE;
      target->kind = svn_node_file;
      target->abs_path = NULL;
      target->wc_path = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_dirent_get_absolute(&abs_wc_path, wc_path, scratch_pool));

  if (svn_dirent_is_absolute(target->canon_path_from_patchfile))
    {
      /* TODO: strip count */
      target->wc_path = svn_dirent_is_child(abs_wc_path,
                                            target->canon_path_from_patchfile,
                                            scratch_pool);
      if (! target->wc_path)
        {
          /* The target path is either outside of the working copy
           * or it is the working copy itself. Skip it. */
          target->skipped = TRUE;
          target->kind = svn_node_file;
          target->abs_path = NULL;
          target->wc_path = NULL;
          return SVN_NO_ERROR;
        }
    }
  else
    {
      /* TODO: strip count */
      target->wc_path = target->canon_path_from_patchfile;
    }

  /* Make sure the path is secure to use. We want the target to be inside
   * of the working copy and not be fooled by symlinks it might contain. */
  if (! svn_dirent_is_under_root(&target->abs_path, abs_wc_path,
                                 target->wc_path, result_pool))
    {
      /* The target path is outside of the working copy. Skip it. */
      target->skipped = TRUE;
      target->kind = svn_node_file;
      target->abs_path = NULL;
      return SVN_NO_ERROR;
    }

  /* Find out if there is a suitable patch target at the target path,
   * and determine if the target should be skipped. */
  SVN_ERR(svn_io_check_path(target->abs_path, &target->kind, scratch_pool));
  switch (target->kind)
    {
      case svn_node_file:
        target->skipped = FALSE;
        break;
      case svn_node_none:
        {
          const char *dirname;
          svn_node_kind_t kind;

          /* The file is not there, that's fine. The patch might want to
           * create it. But the containing directory of the target needs
           * to exists. Otherwise we won't be able to apply the patch. */
          dirname = svn_dirent_dirname(target->abs_path, scratch_pool);
          SVN_ERR(svn_io_check_path(dirname, &kind, scratch_pool));
          target->skipped = (kind != svn_node_dir);
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
 * ADM_ACCESS should hold a write lock to the working copy
 * the patch is being applied to.
 * Use client context CTX to send notifiations.
 * TEMPFILES is a set of tempfiles to use for merging hunks.
 * Upon success, allocate the patch target structure in RESULT_POOL.
 * Else, set *target to NULL.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
init_patch_target(patch_target_t **target, const svn_patch_t *patch,
                  svn_wc_adm_access_t *adm_access,
                  const svn_client_ctx_t *ctx,
                  const hunk_tempfiles_t *tempfiles,
                  apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *new_target;
  const char *dirname;

  *target = NULL;
  new_target = apr_pcalloc(result_pool, sizeof(*new_target));

  SVN_ERR(resolve_target_path(new_target, patch->new_filename,
                              svn_wc_adm_access_path(adm_access),
                              result_pool, scratch_pool));
  if (! new_target->skipped)
    {
      const svn_wc_entry_t *entry;

      /* If the target is versioned, we should be able to get an entry. */
      SVN_ERR(svn_wc__maybe_get_entry(&entry, ctx->wc_ctx,
                                      new_target->abs_path,
                                      svn_node_unknown,
                                      TRUE, FALSE,
                                      result_pool,
                                      scratch_pool));

      if (entry && entry->schedule == svn_wc_schedule_delete)
        {
          /* The target is versioned and scheduled for deletion, so skip it. */
          new_target->skipped = TRUE;
        }
    }

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

  if (! new_target->skipped)
    {
      /* Create a temporary file to write the patched result to,
       * in the same directory as the target file.
       * We want them to be on the same filesystem so we can rename
       * the temporary file to the target file later. */
      dirname = svn_dirent_dirname(new_target->abs_path, scratch_pool);
      SVN_ERR(svn_stream_open_unique(&new_target->result,
                                     &new_target->result_path, dirname,
                                     svn_io_file_del_none,
                                     result_pool, scratch_pool));

      SVN_ERR(check_local_mods(&new_target->local_mods, ctx->wc_ctx,
                               new_target->abs_path, scratch_pool));
     }

  new_target->patch = patch;
  new_target->current_line = 1;
  new_target->modified = FALSE;
  new_target->conflicted = FALSE;
  new_target->added = FALSE;
  new_target->deleted = FALSE;
  new_target->eof = FALSE;
  new_target->tempfiles = tempfiles;

  *target = new_target;
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
  svn_boolean_t hunk_eof;
  svn_boolean_t lines_matched;
  apr_off_t pos;
  apr_pool_t *iterpool;

  *matched = FALSE;

  pos = 0;
  SVN_ERR(svn_io_file_seek(target->file, APR_CUR, &pos, pool));

  svn_stream_reset(hunk->original_text);

  lines_matched = FALSE;
  iterpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(hunk->original_text, &hunk_line,
                                  target->patch->eol_str, &hunk_eof, iterpool));
      SVN_ERR(svn_stream_readline(target->stream, &target_line, target->eol_str,
                                  &target->eof, iterpool));
      if (! hunk_eof && hunk_line->len > 0)
        {
          char c = hunk_line->data[0];
          SVN_ERR_ASSERT(c == ' ' || c == '-');
          lines_matched = (hunk_line->len == target_line->len + 1 &&
                           ! strcmp(hunk_line->data + 1, target_line->data));
        }
    }
  while (lines_matched && ! (hunk_eof || target->eof));
  svn_pool_destroy(iterpool);

  /* Determine whether we had a match. */
  if (hunk_eof)
    *matched = lines_matched;
  else if (target->eof)
    *matched = FALSE;
  
  svn_stream_reset(hunk->original_text);
  SVN_ERR(svn_io_file_seek(target->file, APR_SET, &pos, pool));
  target->eof = FALSE;

  return SVN_NO_ERROR;
}

/* Scan lines of TARGET for a match of the original text of HUNK,
 * up to but not including the specified UPPER_LINE.
 * If UPPER_LINE is zero scan until EOF occurs when reading from TARGET.
 * Indicate in *MATCHED whether HUNK was matched during the scan.
 * If the hunk matched exactly once, return the line number at which
 * the hunk matched in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is TRUE,
 * return the line number at which the first match occured in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is FALSE,
 * return the line number at which the last match occured in *MATCHED_LINE.
 * Do all allocations in POOL. */
static svn_error_t *
scan_for_match(svn_boolean_t *match, svn_linenum_t *matched_line,
               patch_target_t *target, const svn_hunk_t *hunk,
               svn_boolean_t match_first, svn_linenum_t upper_line,
               apr_pool_t *pool)
{
  svn_boolean_t matched;
  apr_pool_t *iterpool;

  *match = FALSE;
  iterpool = svn_pool_create(pool);
  while ((target->current_line < upper_line || upper_line == 0) &&
         ! target->eof)
    {
      svn_stringbuf_t *buf;

      svn_pool_clear(iterpool);

      SVN_ERR(match_hunk(&matched, target, hunk, iterpool));
      if (matched)
        {
          *match = TRUE;
          *matched_line = target->current_line;
          if (match_first)
            break;
        }

      SVN_ERR(svn_stream_readline(target->stream, &buf, target->eol_str,
                                  &target->eof, iterpool));
      if (! target->eof)
        target->current_line++;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Determine the *LINE at which a HUNK applies to the TARGET file.
 * If no correct line can be determined, fall back to the original
 * line offset specified in HUNK -- the user will have to resolve
 * conflicts in this case.
 * When this function returns, neither TARGET->CURRENT_LINE nor the
 * file offset in the target file will have changed.
 * Do temporary allocations in POOL. */
static svn_error_t *
determine_hunk_line(svn_linenum_t *line, patch_target_t *target,
                    const svn_hunk_t *hunk, apr_pool_t *pool)
{
  svn_linenum_t hunk_start;
  svn_boolean_t early_match;
  svn_linenum_t early_matched_line;
  svn_boolean_t match;
  svn_linenum_t matched_line;
  svn_linenum_t saved_line;
  apr_off_t saved_pos;

  saved_line = target->current_line;
  saved_pos = 0;
  SVN_ERR(svn_io_file_seek(target->file, APR_CUR, &saved_pos, pool));

  /* If the file didn't originally exist, the starting line is zero,
   * but we're counting lines starting from 1 so fix that up. */
  hunk_start = hunk->original_start == 0 ? 1 : hunk->original_start;

  /* Scan forward towards the hunk's line and look for a line where the
   * hunk matches, in case there are local changes in the target which 
   * cause the hunk to match early. */
  SVN_ERR(scan_for_match(&early_match, &early_matched_line, target, hunk,
                         FALSE, hunk_start, pool));

  /* Scan for a match at the line where the hunk thinks it should be going. */
  SVN_ERR(scan_for_match(&match, &matched_line, target, hunk, TRUE,
                         hunk_start + 1, pool));
  if (match)
    {
      /* Neat, an exact match. */
      SVN_ERR_ASSERT(matched_line == hunk_start);
      *line = hunk_start;
    }
  else
    {
      /* Scan forward towards the end of the file and look for a line where
       * the hunk matches, in case there are local changes in the target
       * which cause the hunk to match late. */
      SVN_ERR(scan_for_match(&match, &matched_line, target, hunk, TRUE, 0,
                             pool));
      if (match && ! early_match)
        {
          /* We have a late match only, so use it. */
          *line = matched_line;
        }
      else if (! match && early_match)
        {
          /* We have a early match only, so use it. */
          *line = early_matched_line;
        }
      else if (match && early_match)
        {
          apr_off_t early_offset;
          apr_off_t late_offset;

          /* We have both a late and an early match. Use whichever is
           * closest to where the hunk thinks it should be going. */
          SVN_ERR_ASSERT(early_matched_line < hunk_start);
          SVN_ERR_ASSERT(matched_line > hunk_start);
          early_offset = hunk_start - early_matched_line;
          late_offset = matched_line - hunk_start;

          if (early_offset < late_offset)
            *line = early_matched_line;
          else if (early_offset > late_offset)
            *line = matched_line;
          else if (early_offset == late_offset)
            {
              /* But don't try to be smart about breaking a tie.
               * Just apply the hunk where it thinks it should be going.
               * There will be conflicts. */
              *line = hunk_start;
            }
        }
      else
        {
          /* We found no match at all.
           * Just apply the hunk where it thinks it should be going.
           * There will be conflicts. */
          *line = hunk_start;
        }
    }

  target->current_line = saved_line;
  SVN_ERR(svn_io_file_seek(target->file, APR_SET, &saved_pos, pool));
  target->eof = FALSE;

  return SVN_NO_ERROR;
}

/* Copy lines to the result stream of TARGET until the specified
 * LINE has been reached. Indicate in *EOF whether end-of-file was
 * encountered while reading from the target.
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
      svn_stringbuf_t *buf;
      apr_size_t len;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(target->stream, &buf, target->eol_str,
                                  &target->eof, iterpool));
      if (! target->eof)
        {
          svn_stringbuf_appendcstr(buf, target->eol_str);
          target->current_line++;
        }

      len = buf->len;
      SVN_ERR(svn_stream_write(target->result, buf->data, &len));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Read at most NLINES from the TARGET file, returning lines read in *LINES,
 * separated by the EOL sequence specified in TARGET->patch_eol_str.
 * Allocate *LINES in RESULT_POOL.
 * The caller is responsible for closing *LINES.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
read_lines_from_target(svn_stream_t **lines, svn_linenum_t nlines,
                       patch_target_t *target, apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  apr_pool_t *iterpool;
  svn_linenum_t i;
  apr_off_t start, end;
  apr_int32_t flags = APR_READ | APR_BUFFERED;

  start = 0;
  SVN_ERR(svn_io_file_seek(target->file, APR_CUR, &start, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < nlines; i++)
    {
      svn_stringbuf_t *line;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(target->stream, &line, target->eol_str,
                                  &target->eof, iterpool));
      if (target->eof)
        break;
      else
        target->current_line++;
    }
  svn_pool_destroy(iterpool);

  end = 0;
  SVN_ERR(svn_io_file_seek(target->file, APR_CUR, &end, scratch_pool));

  SVN_ERR(svn_io_file_open(&file, target->abs_path, flags, APR_OS_DEFAULT,
                           result_pool));
  *lines = svn_stream_from_aprfile_range_readonly(file, FALSE, start, end,
                                                  result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_hunk_text(svn_stream_t *hunk_text, apr_file_t *file,
               const char *target_eol_str, const char *patch_eol_str,
               apr_pool_t *scratch_pool)
{
  svn_boolean_t eof;
  apr_pool_t *iterpool;
  apr_off_t pos;

  /* Rewind temp file. */
  pos = 0;
  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;
      apr_size_t len;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(hunk_text, &line, patch_eol_str,
                                  &eof, iterpool));
      if (! eof)
        {
          if (line->len >= 1)
            {
              char c = line->data[0];

              SVN_ERR_ASSERT(c == ' ' || c == '+' || c == '-');
              len = line->len - 1;
              SVN_ERR(svn_io_file_write_full(file, line->data + 1, len, &len,
                                             iterpool));
              SVN_ERR_ASSERT(len == line->len - 1);
            }

          /* Add newline. */
          len = strlen(target_eol_str);
          SVN_ERR(svn_io_file_write_full(file, target_eol_str, len, &len,
                                         iterpool));
          SVN_ERR_ASSERT(len == strlen(target_eol_str));
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  /* Truncate and flush temporary file. */
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &pos, scratch_pool));
  SVN_ERR(svn_io_file_trunc(file, pos, scratch_pool));
  SVN_ERR(svn_io_file_flush_to_disk(file, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_latest_text(svn_stream_t *latest_text, apr_file_t *file,
                 const char *target_eol_str, const char *patch_eol_str,
                 apr_pool_t *scratch_pool)
{
  svn_stream_t *disowned_stream;
  svn_stream_t *disowned_latest_text;
  apr_off_t pos;

  /* Since we use the latest text verbatim, we can do a direct stream copy. */
  pos = 0;
  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, scratch_pool));
  /* Make sure to disown the streams, we don't want underlying
   * files to be closed. */
  disowned_stream = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  disowned_latest_text = svn_stream_disown(latest_text, scratch_pool);
  SVN_ERR(svn_stream_copy3(disowned_latest_text, disowned_stream, NULL, NULL,
                           scratch_pool));
  SVN_ERR(svn_stream_close(disowned_stream));
  SVN_ERR(svn_stream_close(disowned_latest_text));

  /* Truncate and flush temporary file. */
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &pos, scratch_pool));
  SVN_ERR(svn_io_file_trunc(file, pos, scratch_pool));
  SVN_ERR(svn_io_file_flush_to_disk(file, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
merge_hunk(patch_target_t *target, const svn_hunk_t *hunk,
           svn_stream_t *latest_text, apr_pool_t *pool)
{
  svn_diff_t *diff;
  svn_diff_file_options_t *opts;

  /* Copy original hunk text into temporary file. */
  SVN_ERR(copy_hunk_text(hunk->original_text, target->tempfiles->orig_file,
                         target->eol_str, target->patch->eol_str,
                         pool));

  /* Copy modified hunk text into temporary file. */
  SVN_ERR(copy_hunk_text(hunk->modified_text, target->tempfiles->mod_file,
                         target->eol_str, target->patch->eol_str,
                         pool));

  /* Copy latest text as it appeared in target into temporary file. */
  SVN_ERR(copy_latest_text(latest_text, target->tempfiles->latest_file,
                           target->eol_str, target->patch->eol_str,
                           pool));

  /* Diff the hunks. */
  opts = svn_diff_file_options_create(pool);
  SVN_ERR(svn_diff_file_diff3_2(&diff, target->tempfiles->orig_path,
                                target->tempfiles->mod_path,
                                target->tempfiles->latest_path,
                                opts, pool));
  if (svn_diff_contains_diffs(diff))
    {
      svn_diff_conflict_display_style_t conflict_style;

      /* TODO: Make conflict style configurable? */
      conflict_style = svn_diff_conflict_display_modified_original_latest;

      /* Merge the hunks. */
      SVN_ERR(svn_diff_file_output_merge2(target->result, diff,
                                          target->tempfiles->orig_path,
                                          target->tempfiles->mod_path,
                                          target->tempfiles->latest_path,
                                          NULL, NULL, NULL, NULL,
                                          conflict_style, pool));
      target->modified = TRUE;
      if (! target->conflicted)
        target->conflicted = svn_diff_contains_conflicts(diff);
    }

  return SVN_NO_ERROR;
}

/* Apply a HUNK to a patch TARGET. Do all allocations in POOL. */
static svn_error_t *
apply_one_hunk(const svn_hunk_t *hunk, patch_target_t *target, apr_pool_t *pool)
{
  svn_linenum_t hunk_line;
  svn_stream_t *latest_text;

  if (target->kind == svn_node_file)
    {
      /* Determine the line the hunk should be applied at. */
      SVN_ERR(determine_hunk_line(&hunk_line, target, hunk, pool));

      if (target->current_line > hunk_line)
        {
          /* If we already passed the line that the hunk should be applied to,
           * the hunks in the patch file are out of order. */
          /* TODO: Warn, create reject file? */
          return SVN_NO_ERROR;
        }

      /* Move forward to the hunk's line, copying data as we go. */
      if (target->current_line < hunk_line)
        SVN_ERR(copy_lines_to_target(target, hunk_line, pool));
      if (target->eof)
        {
          /* File is shorter than it should be. */
          /* TODO: Warn, create reject file? */
          return SVN_NO_ERROR;
        }

      /* Target file is at the hunk's line.
       * Read the target's version of the hunk.
       * We assume the target hunk has the same length as the original hunk.
       * If that's not the case, we'll get merge conflicts. */
      SVN_ERR(read_lines_from_target(&latest_text, hunk->original_length,
                                     target, pool, pool));
    }
  else
    {
      /* We're creating a new file, so the latest text is simply empty. */
      latest_text = svn_stream_empty(pool);
    }

  SVN_ERR(merge_hunk(target, hunk, latest_text, pool));

  SVN_ERR(svn_stream_close(latest_text));

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for a patch TARGET.
 * Send WC_PATH as the working copy path in notifications.
 * Use POOL for temporary allocations. */
static svn_error_t *
maybe_send_patch_target_notification(const patch_target_t *target,
                                     const char *wc_path,
                                     const svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  svn_wc_notify_t *notify;
  svn_wc_notify_action_t action;
  const char *path;

  if (! ctx->notify_func2)
    return SVN_NO_ERROR;

  if (target->skipped)
    action = svn_wc_notify_skip;
  else if (target->deleted)
    action = svn_wc_notify_update_delete;
  else if (target->added)
    action = svn_wc_notify_update_add;
  else
    action = svn_wc_notify_update_update;

  /* Figure out which path to report for the patch target. */
  if (! target->skipped && target->wc_path)
    path = svn_dirent_join(wc_path, target->wc_path, pool);
  else
    path = target->canon_path_from_patchfile;

  notify = svn_wc_create_notify(path, action, pool);
  notify->kind = svn_node_file;

  if (action == svn_wc_notify_skip)
    {
      if (target->kind == svn_node_none)
        notify->content_state = svn_wc_notify_state_missing;
      else if (target->kind == svn_node_dir)
        notify->content_state = svn_wc_notify_state_obstructed;
      else
        notify->content_state = svn_wc_notify_state_unknown;
    }
  else
    {
      if (target->conflicted)
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

/* Apply a PATCH to a working copy at WC_PATH.
 * Use client context CTX to send notifiations.
 * ADM_ACCESS should hold a write lock to the WC_PATH.
 * TEMPFILES is a set of tempfiles to use for merging hunks.
 * Do all allocations in POOL. */
static svn_error_t *
apply_one_patch(svn_patch_t *patch, const char *wc_path,
                svn_wc_adm_access_t *adm_access,
                const hunk_tempfiles_t *tempfiles, svn_boolean_t dry_run,
                const svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  patch_target_t *target;

  SVN_ERR(init_patch_target(&target, patch, adm_access, ctx,
                            tempfiles, pool, pool));
  if (target == NULL)
    /* Can't apply the patch. */
    return SVN_NO_ERROR;

  if (! target->skipped)
    {
      svn_hunk_t *hunk;
      apr_pool_t *iterpool;

      /* Apply hunks. */
      iterpool = svn_pool_create(pool);
      do
        {
          svn_pool_clear(iterpool);

          SVN_ERR(svn_diff__parse_next_hunk(&hunk, patch, iterpool, iterpool));
          if (hunk)
            {
              SVN_ERR(apply_one_hunk(hunk, target, iterpool));
              SVN_ERR(svn_diff__destroy_hunk(hunk));
            }

        }
      while (hunk);
      svn_pool_destroy(iterpool);

      if (target->kind == svn_node_file)
        {
          /* Copy any remaining lines to target. */
          SVN_ERR(copy_lines_to_target(target, 0, pool));
          if (! target->eof)
            {
              /* We could not copy the entire target file to the temporary file,
               * and would truncate the target if we moved the temporary file
               * on top of it. Cancel any modifications to the target file and
               * report is as skipped. TODO: Dump hunks into reject file? */
              target->modified = FALSE;
              target->skipped = TRUE;
            }

          /* Closing this stream will also close the underlying file. */
          SVN_ERR(svn_stream_close(target->stream));
        }

      SVN_ERR(svn_stream_close(target->result));

      if (target->modified)
        {
          apr_finfo_t old_file;
          apr_finfo_t new_file;

          /* Get sizes of the patched temporary file (new) and the
           * working file (old). We'll need those to figure out whether
           * we should add or delete the patched file. */
          SVN_ERR(svn_io_stat(&new_file, target->result_path, APR_FINFO_SIZE,
                              pool));
          if (target->kind == svn_node_file)
            SVN_ERR(svn_io_stat(&old_file, target->abs_path, APR_FINFO_SIZE,
                                pool));
          else
            old_file.size = 0;

          if (new_file.size >= 0 && old_file.size == 0)
            {
              /* If the target did not exist we've just added it.
               * If it did exist the target was empty before patching,
               * and maybe it is still empty now. */
              target->added = (target->kind == svn_node_none);
            }
          else if (new_file.size == 0 && old_file.size > 0)
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
                  svn_wc_adm_access_t *parent_adm_access;
                  const char *dirname;

                  /* Schedule the target for deletion.  Suppress
                   * notification, we'll do it manually in a minute. */
                  dirname = svn_dirent_dirname(target->abs_path, pool);
                  SVN_ERR(svn_wc_adm_retrieve(&parent_adm_access, adm_access,
                                              dirname, pool));
                  SVN_ERR(svn_wc_delete3(target->abs_path, parent_adm_access,
                                         ctx->cancel_func, ctx->cancel_baton,
                                         NULL, NULL, FALSE /* keep_local */,
                                         pool));
                }

              /* Remove the tempfile, too. */
              SVN_ERR(svn_io_remove_file2(target->result_path, FALSE,
                                          pool));
            }
          else
            {
              if (old_file.size == 0 && new_file.size == 0)
                {
                  /* The target was empty or non-existent to begin with
                   * and nothing has changed by patching. Just remove the
                   * temporary file and report this as skipped if it didn't
                   * exist. */
                  SVN_ERR(svn_io_remove_file2(target->result_path, FALSE,
                                              pool));
                  target->added = FALSE;
                  if (target->kind == svn_node_none)
                    target->skipped = TRUE;
                }
              else
                {
                  if (dry_run)
                    {
                      /* Just remove the temporary file. */
                      SVN_ERR(svn_io_remove_file2(target->result_path, FALSE,
                                                  pool));
                    }
                  else
                    {
                      /* Install patched temporary file over working file. 
                       * ### Should this rather be done in a loggy fashion? */
                      SVN_ERR(svn_io_file_rename(target->result_path,
                                               target->abs_path, pool));

                      if (target->added)
                        {
                          /* The target file didn't exist previously, so add
                           * it to version control. Suppress notification,
                           * we'll do it manually in a minute. */
                          SVN_ERR(svn_wc_add3(target->abs_path, adm_access,
                                              svn_depth_infinity,
                                              NULL, SVN_INVALID_REVNUM,
                                              ctx->cancel_func,
                                              ctx->cancel_baton,
                                              NULL, NULL, pool));
                        }
                    }
                }
            }
        }
      else
        {
          /* No hunks were applied. Just remove the temporary file. */
          SVN_ERR(svn_io_remove_file2(target->result_path, FALSE, pool));
        }
    }

  SVN_ERR(maybe_send_patch_target_notification(target, wc_path, ctx, pool));

  return SVN_NO_ERROR;
}

/* Apply all diffs in the patch file at PATCH_PATH to the working copy
 * at WC_PATH. ADM_ACCESS should hold a write lock to the working copy.
 * Use client context CTX to send notifiations.
 * Do all allocations in POOL.  */
static svn_error_t *
apply_textdiffs(const char *patch_path, const char *wc_path,
                svn_wc_adm_access_t *adm_access, svn_boolean_t dry_run,
                const svn_client_ctx_t *ctx, apr_pool_t *pool)
{
  svn_patch_t *patch;
  apr_pool_t *iterpool;
  const char *patch_eol_str;
  apr_file_t *patch_file;
  hunk_tempfiles_t *tempfiles;

  /* Try to open the patch file. */
  SVN_ERR(svn_io_file_open(&patch_file, patch_path,
                           APR_READ | APR_BINARY, 0, pool));

  SVN_ERR(svn_eol__detect_file_eol(&patch_eol_str, patch_file, pool));
  if (patch_eol_str == NULL)
    {
      /* If we can't figure out the EOL scheme, just assume native.
       * It's most likely a bad patch file anyway that will fail to
       * apply later. */
      patch_eol_str = APR_EOL_STR;
    }

  tempfiles = apr_pcalloc(pool, sizeof(tempfiles));

  /* Create temporary files for hunk-merging. */
  SVN_ERR(svn_io_mktemp(&tempfiles->orig_file,
                        &tempfiles->orig_path, NULL, "svnpatch-orig",
                        svn_io_file_del_on_close, pool, pool));
  SVN_ERR(svn_io_mktemp(&tempfiles->mod_file,
                        &tempfiles->mod_path, NULL, "svnpatch-mod",
                        svn_io_file_del_on_close, pool, pool));
  SVN_ERR(svn_io_mktemp(&tempfiles->latest_file,
                        &tempfiles->latest_path, NULL, "svnpatch-latest",
                        svn_io_file_del_on_close, pool, pool));

  /* Apply patches. */
  iterpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_diff__parse_next_patch(&patch, patch_file, patch_eol_str,
                                         iterpool, iterpool));
      if (patch)
        SVN_ERR(apply_one_patch(patch, wc_path, adm_access, tempfiles,
                                dry_run, ctx, iterpool));
    }
  while (patch);
  svn_pool_destroy(iterpool);

  /* Clean up temporary files. */
  SVN_ERR(svn_io_file_close(tempfiles->orig_file, pool));
  SVN_ERR(svn_io_file_close(tempfiles->mod_file, pool));
  SVN_ERR(svn_io_file_close(tempfiles->latest_file, pool));

  return SVN_NO_ERROR;
}
