/*
 * file-merge.c: internal file merge tool
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

/* This is an interactive file merge tool with an interface similar to
 * the interactive mode of the UNIX sdiff ("side-by-side diff") utility.
 * The merge tool is driven by Subversion's diff code and user input. */

#include "svn_cmdline.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_dep_compat.h"

/* Baton for functions in this file which implement svn_diff_output_fns_t. */
struct file_merge_baton {
  /* The files being merged. */
  apr_file_t *original_file;
  apr_file_t *modified_file;
  apr_file_t *latest_file;
  
  /* Counters to keep track of the current line in each file. */
  svn_linenum_t current_line_original;
  svn_linenum_t current_line_modified;
  svn_linenum_t current_line_latest;

  /* The merge result is written to this file. */
  apr_file_t *merged_file;

  /* Whether the merged file remains in conflict after the merge. */
  svn_boolean_t remains_in_conflict;

  /* Pool for temporary allocations. */
  apr_pool_t *scratch_pool;
} file_merge_baton;

/* A helper for reading a line of text from a range in a file.
 *
 * Allocate *STRINGBUF in RESULT_POOL, and read into it one line from FILE.
 * Reading stops either after a line-terminator was found or after MAX_LEN
 * bytes have been read. The line-terminator is not stored in *STRINGBUF.
 *
 * The line-terminator is detected automatically and stored in *EOL
 * if EOL is not NULL. If EOF is reached and FILE does not end
 * with a newline character, and EOL is not NULL, *EOL is set to NULL.
 *
 * SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
readline(apr_file_t *file,
         svn_stringbuf_t **stringbuf,
         const char **eol,
         svn_boolean_t *eof,
         apr_size_t max_len,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *str;
  const char *eol_str;
  apr_size_t numbytes;
  char c;
  apr_size_t len;
  svn_boolean_t found_eof;

  str = svn_stringbuf_create_ensure(80, result_pool);

  /* Read bytes into STR up to and including, but not storing,
   * the next EOL sequence. */
  eol_str = NULL;
  numbytes = 1;
  len = 0;
  found_eof = FALSE;
  while (!found_eof)
    {
      if (len < max_len)
        SVN_ERR(svn_io_file_read_full2(file, &c, sizeof(c), &numbytes,
                                       &found_eof, scratch_pool));
      len++;
      if (numbytes != 1 || len > max_len)
        {
          found_eof = TRUE;
          break;
        }

      if (c == '\n')
        {
          eol_str = "\n";
        }
      else if (c == '\r')
        {
          eol_str = "\r";

          if (!found_eof && len < max_len)
            {
              apr_off_t pos;

              /* Check for "\r\n" by peeking at the next byte. */
              pos = 0;
              SVN_ERR(svn_io_file_seek(file, APR_CUR, &pos, scratch_pool));
              SVN_ERR(svn_io_file_read_full2(file, &c, sizeof(c), &numbytes,
                                             &found_eof, scratch_pool));
              if (numbytes == 1 && c == '\n')
                {
                  eol_str = "\r\n";
                  len++;
                }
              else
                {
                  /* Pretend we never peeked. */
                  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, scratch_pool));
                  found_eof = FALSE;
                  numbytes = 1;
                }
            }
        }
      else
        svn_stringbuf_appendbyte(str, c);

      if (eol_str)
        break;
    }

  if (eol)
    *eol = eol_str;
  if (eof)
    *eof = found_eof;
  *stringbuf = str;

  return SVN_NO_ERROR;
}

/* Copy LEN lines from SOURCE_FILE to the MERGED_FILE, starting at
 * line START. The CURRENT_LINE is the current line in the source file.
 * The new current line is returned in *NEW_CURRENT_LINE. */
static svn_error_t *
copy_to_merged_file(svn_linenum_t *new_current_line,
                    apr_file_t *merged_file,
                    apr_file_t *source_file,
                    apr_off_t start,
                    apr_off_t len,
                    svn_linenum_t current_line,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  svn_stringbuf_t *line;
  apr_size_t lines_read;
  apr_size_t lines_copied;
  svn_boolean_t eof;
  svn_linenum_t orig_current_line = current_line;

  lines_read = 0;
  iterpool = svn_pool_create(scratch_pool);
  while (current_line < start)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(readline(source_file, &line, NULL, &eof, APR_SIZE_MAX,
                       iterpool, iterpool));
      if (eof)
        break;

      current_line++;
      lines_read++;
    }

  lines_copied = 0;
  while (lines_copied < len)
    {
      apr_size_t bytes_written;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(readline(source_file, &line, &eol_str, &eof, APR_SIZE_MAX,
                       iterpool, iterpool));
      if (eol_str)
        svn_stringbuf_appendcstr(line, eol_str);
      SVN_ERR(svn_io_file_write_full(merged_file, line->data, line->len,
                                     &bytes_written, iterpool));
      if (bytes_written != line->len)
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL,
                                _("Could not write data to merged file"));
      if (eof)
        break;
      lines_copied++;
    }
  svn_pool_destroy(iterpool);

  *new_current_line = orig_current_line + lines_read + lines_copied;

  return SVN_NO_ERROR;
}

/* Copy common data to the merged file. */
static svn_error_t *
file_merge_output_common(void *output_baton,
                         apr_off_t original_start,
                         apr_off_t original_length,
                         apr_off_t modified_start,
                         apr_off_t modified_length,
                         apr_off_t latest_start,
                         apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  SVN_ERR(copy_to_merged_file(&b->current_line_original,
                              b->merged_file,
                              b->original_file,
                              original_start,
                              original_length,
                              b->current_line_original,
                              b->scratch_pool));
  return SVN_NO_ERROR;
}

/* Original/latest match up, but modified differs.
 * Copy modified data to the merged file. */
static svn_error_t *
file_merge_output_diff_modified(void *output_baton,
                                apr_off_t original_start,
                                apr_off_t original_length,
                                apr_off_t modified_start,
                                apr_off_t modified_length,
                                apr_off_t latest_start,
                                apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  SVN_ERR(copy_to_merged_file(&b->current_line_modified,
                              b->merged_file,
                              b->modified_file,
                              modified_start,
                              modified_length,
                              b->current_line_modified,
                              b->scratch_pool));

  return SVN_NO_ERROR;
}

/* Original/modified match up, but latest differs.
 * Copy latest data to the merged file. */
static svn_error_t *
file_merge_output_diff_latest(void *output_baton,
                              apr_off_t original_start,
                              apr_off_t original_length,
                              apr_off_t modified_start,
                              apr_off_t modified_length,
                              apr_off_t latest_start,
                              apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  SVN_ERR(copy_to_merged_file(&b->current_line_latest,
                              b->merged_file,
                              b->latest_file,
                              latest_start,
                              latest_length,
                              b->current_line_latest,
                              b->scratch_pool));

  return SVN_NO_ERROR;
}

/* Modified/latest match up, but original differs.
 * Copy latest data to the merged file. */
static svn_error_t *
file_merge_output_diff_common(void *output_baton,
                              apr_off_t original_start,
                              apr_off_t original_length,
                              apr_off_t modified_start,
                              apr_off_t modified_length,
                              apr_off_t latest_start,
                              apr_off_t latest_length)
{
  struct file_merge_baton *b = output_baton;

  SVN_ERR(copy_to_merged_file(&b->current_line_latest,
                              b->merged_file,
                              b->latest_file,
                              latest_start,
                              latest_length,
                              b->current_line_latest,
                              b->scratch_pool));
  return SVN_NO_ERROR;
}


/* Return LEN lines within the diff chunk staring at line START
 * in a *LINES array of svn_stringbuf_t* elements.
 * Store the resulting current in in *NEW_CURRENT_LINE. */
static svn_error_t *
read_diff_chunk(apr_array_header_t **lines,
                svn_linenum_t *new_current_line,
                apr_file_t *file,
                svn_linenum_t current_line,
                apr_off_t start,
                apr_off_t len,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  const char *eol_str;
  svn_boolean_t eof;
  apr_pool_t *iterpool;

  *lines = apr_array_make(result_pool, 0, sizeof(svn_stringbuf_t *));

  /* Skip lines before start of range. */
  iterpool = svn_pool_create(scratch_pool);
  while (current_line < start)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(readline(file, &line, NULL, &eof, APR_SIZE_MAX,
                       iterpool, iterpool));
      if (eof)
        return SVN_NO_ERROR;
      current_line++;
    }
  svn_pool_destroy(iterpool);

  /* Now read the lines. */
  do
    {
      SVN_ERR(readline(file, &line, &eol_str, &eof, APR_SIZE_MAX,
                       result_pool, scratch_pool));
      if (eol_str)
        svn_stringbuf_appendcstr(line, eol_str);
      if (line->len > 0)
        APR_ARRAY_PUSH(*lines, svn_stringbuf_t *) = line;
      if (eof)
        break;
      current_line++;
    }
  while ((*lines)->nelts < len);

  *new_current_line = current_line;

  return SVN_NO_ERROR;
}

/* ### make this configurable? */
#define MAX_LINE_DISPLAY_LEN ((80 / 2) - 4)

/* Prepare LINE for display, pruning or extending it to MAX_LINE_DISPLAY_LEN
 * characters, and stripping the EOL marker, if any. */
static const char *
prepare_line_for_display(const char *line, apr_pool_t *result_pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create(line, result_pool);
  int max_len = MAX_LINE_DISPLAY_LEN;

  /* Trim EOL. */
  if (buf->len > 2 &&
      buf->data[buf->len - 2] == '\r' && 
      buf->data[buf->len - 1] == '\n')
    svn_stringbuf_chop(buf, 2);
  else if (buf->len > 1 &&
           (buf->data[buf->len - 1] == '\n' ||
            buf->data[buf->len - 1] == '\r'))
    svn_stringbuf_chop(buf, 1);

  /* Trim further in case line is still too long, or add padding in case
   * it is too short. */
  if (buf->len > 0 && ((buf->len - 1) > max_len))
    svn_stringbuf_chop(buf, (buf->len - 1) - max_len);
  else
    {
      while (buf->len == 0 || ((buf->len - 1) < max_len))
        svn_stringbuf_appendbyte(buf, ' ');
    }

  SVN_ERR_ASSERT_NO_RETURN((buf->len - 1) ==  MAX_LINE_DISPLAY_LEN);
  return buf->data;
}

#define SEP_STRING \
  "--------------------------------------+--------------------------------------\n"

/* Merge chunks CHUNK1 and CHUNK2.
 * Each lines array contains elements of type svn_stringbuf_t*.
 * Return the result in *MERGED_CHUNK, or set *MERGED_CHUNK to NULL in
 * case the user chooses to postpone resolution of this chunk. */
static svn_error_t *
merge_chunks(apr_array_header_t **merged_chunk,
             apr_array_header_t *chunk1,
             apr_array_header_t *chunk2,
             svn_linenum_t current_line1,
             svn_linenum_t current_line2,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *prompt;
  int i;
  int max_chunk_lines;
  apr_pool_t *iterpool;

  max_chunk_lines = chunk1->nelts > chunk2->nelts ? chunk1->nelts
                                                  : chunk2->nelts;
  /* 
   * Prepare the selection prompt.
   */

  prompt = svn_stringbuf_create(
             apr_psprintf(scratch_pool, "%s | %s\n%s",
                          prepare_line_for_display(
                            apr_psprintf(scratch_pool,
                                         _("(1) their version (at line %lu)"),
                                         current_line1),
                            scratch_pool),
                          prepare_line_for_display(
                            apr_psprintf(scratch_pool,
                                         _("(2) your version (at line %lu)"),
                                         current_line2),
                            scratch_pool),
                          SEP_STRING),
             scratch_pool);

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < max_chunk_lines; i++)
    {
      const char *line1;
      const char *line2;
      const char *prompt_line;

      svn_pool_clear(iterpool);

      if (i < chunk1->nelts)
        line1 = prepare_line_for_display(
                  APR_ARRAY_IDX(chunk1, i, svn_stringbuf_t*)->data,
                  iterpool);
      else
        line1 = prepare_line_for_display("", iterpool);

      if (i < chunk2->nelts)
        line2 = prepare_line_for_display(
                  APR_ARRAY_IDX(chunk2, i, svn_stringbuf_t*)->data,
                  iterpool);
      else
        line2 = prepare_line_for_display("", iterpool);
        
      prompt_line = apr_psprintf(iterpool, "%s | %s\n", line1, line2);

      svn_stringbuf_appendcstr(prompt, prompt_line);
    }
  svn_pool_destroy(iterpool);

  svn_stringbuf_appendcstr(prompt, SEP_STRING);
  svn_stringbuf_appendcstr(
    prompt,
    _("Select: (1) their version, (2) your version, (p) postpone: "));

  /* ### TODO: Offer the option of editing either or both file chunks
   * into a merged chunk. */

  /* Now let's see what the user wants to do with this conflict. */
  while (TRUE)
    {
      const char *answer;

      SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt->data, NULL, scratch_pool));
      if (strcmp(answer, "1") == 0)
        {
          *merged_chunk = chunk1;
          break;
        }
      else if (strcmp(answer, "2") == 0)
        {
          *merged_chunk = chunk2;
          break;
        }
      else if (strcmp(answer, "p") == 0)
        {
          *merged_chunk = NULL;
          break;
        }
    }

  return SVN_NO_ERROR;
}

/* Perform a merge of chunks from FILE1 and FILE2, specified by START1/LEN1
 * and START2/LEN2, respectively. Append the result to MERGED_FILE.
 * The current line numbers for FILE1 and FILE2 are passed in *CURRENT_LINE1
 * and *CURRENT_LINE2, and will be updated to new values upon return. */
static svn_error_t *
merge_file_chunks(svn_boolean_t *remains_in_conflict,
                  apr_file_t *merged_file,
                  apr_file_t *file1,
                  apr_file_t *file2,
                  apr_off_t start1,
                  apr_off_t len1,
                  apr_off_t start2,
                  apr_off_t len2,
                  svn_linenum_t *current_line1,
                  svn_linenum_t *current_line2,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *chunk1;
  apr_array_header_t *chunk2;
  apr_array_header_t *merged_chunk;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(read_diff_chunk(&chunk1, current_line1, file1, *current_line1,
                          start1, len1, scratch_pool, scratch_pool));
  SVN_ERR(read_diff_chunk(&chunk2, current_line2, file2, *current_line2,
                          start2, len2, scratch_pool, scratch_pool));

  SVN_ERR(merge_chunks(&merged_chunk, chunk1, chunk2,
                       *current_line1, *current_line2,
                       scratch_pool, scratch_pool));

  /* If the user chose 'postpone' put conflict markers and left/right
   * versions into the merged file. */
  if (merged_chunk == NULL)
    {
      *remains_in_conflict = TRUE;
        
      merged_chunk = apr_array_make(scratch_pool, 0,
                                    sizeof(svn_stringbuf_t *));
      /* ### would be nice to show filenames next to conflict markers */
      APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
        svn_stringbuf_create("<<<<<<<\n", scratch_pool);
      for (i = 0; i < chunk1->nelts; i++)
        {
          APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
            APR_ARRAY_IDX(chunk1, i, svn_stringbuf_t*);
        }
      APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
        svn_stringbuf_create("=======\n", scratch_pool);
      for (i = 0; i < chunk2->nelts; i++)
        {
          APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
            APR_ARRAY_IDX(chunk2, i, svn_stringbuf_t*);
        }
      APR_ARRAY_PUSH(merged_chunk, svn_stringbuf_t *) =
        svn_stringbuf_create(">>>>>>>\n", scratch_pool);
    }

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < merged_chunk->nelts; i++)
    {
      apr_size_t bytes_written;
      svn_stringbuf_t *line = APR_ARRAY_IDX(merged_chunk, i,
                                            svn_stringbuf_t *);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_file_write_full(merged_file, line->data, line->len,
                                     &bytes_written, iterpool));
      if (line->len != bytes_written)
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL,
                                _("Could not write data to merged file"));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Original, modified, and latest all differ from one another.
 * This is a conflict and we'll need to ask the user to merge it. */
static svn_error_t *
file_merge_output_conflict(void *output_baton,
                           apr_off_t original_start,
                           apr_off_t original_length,
                           apr_off_t modified_start,
                           apr_off_t modified_length,
                           apr_off_t latest_start,
                           apr_off_t latest_length,
                           svn_diff_t *resolved_diff)
{
  struct file_merge_baton *b = output_baton;

  SVN_ERR(merge_file_chunks(&b->remains_in_conflict,
                            b->merged_file,
                            b->modified_file,
                            b->latest_file,
                            modified_start,
                            modified_length,
                            latest_start,
                            latest_length,
                            &b->current_line_modified,
                            &b->current_line_latest,
                            b->scratch_pool));
  return SVN_NO_ERROR;
}

/* Our collection of diff output functions that get driven during the merge. */
static svn_diff_output_fns_t file_merge_diff_output_fns = {
  file_merge_output_common,
  file_merge_output_diff_modified,
  file_merge_output_diff_latest,
  file_merge_output_diff_common,
  file_merge_output_conflict
};

svn_error_t *
svn_cl__merge_file(const char *base_path,
                   const char *their_path,
                   const char *my_path,
                   const char *merged_path,
                   const char *wc_path,
                   apr_hash_t *config,
                   svn_boolean_t *remains_in_conflict,
                   apr_pool_t *scratch_pool)
{
  svn_diff_t *diff;
  svn_diff_file_options_t *diff_options;
  apr_file_t *original_file;
  apr_file_t *modified_file;
  apr_file_t *latest_file;
  apr_file_t *merged_file;
  struct file_merge_baton fmb;

  SVN_ERR(svn_io_file_open(&original_file, base_path,
                           APR_READ|APR_BUFFERED|APR_BINARY,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_open(&modified_file, their_path,
                           APR_READ|APR_BUFFERED|APR_BINARY,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_open(&latest_file, my_path,
                           APR_READ|APR_BUFFERED|APR_BINARY,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_open(&merged_file, merged_path,
                           APR_WRITE|APR_TRUNCATE|APR_BUFFERED|APR_BINARY,
                           APR_OS_DEFAULT, scratch_pool));

  diff_options = svn_diff_file_options_create(scratch_pool);
  SVN_ERR(svn_diff_file_diff3_2(&diff, base_path, their_path, my_path,
                                diff_options, scratch_pool));

  fmb.original_file = original_file;
  fmb.modified_file = modified_file;
  fmb.latest_file = latest_file;
  fmb.current_line_original = 0;
  fmb.current_line_modified = 0;
  fmb.current_line_latest = 0;
  fmb.merged_file = merged_file;
  fmb.remains_in_conflict = FALSE;
  fmb.scratch_pool = scratch_pool;

  SVN_ERR(svn_diff_output(diff, &fmb, &file_merge_diff_output_fns));

  SVN_ERR(svn_io_file_close(original_file, scratch_pool));
  SVN_ERR(svn_io_file_close(modified_file, scratch_pool));
  SVN_ERR(svn_io_file_close(latest_file, scratch_pool));
  SVN_ERR(svn_io_file_close(merged_file, scratch_pool));

  if (remains_in_conflict)
    *remains_in_conflict = fmb.remains_in_conflict;

  return SVN_NO_ERROR;
}
