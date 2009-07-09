/*
 * parse-diff.c: functions for parsing diff files
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

#include <limits.h>  /* for ULONG_MAX */

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_dirent_uri.h"

#include "private/svn_diff_private.h"


/* Helper macro for readability */
#define starts_with(str, start)  \
  (strncmp((str), (start), strlen(start)) == 0)


svn_error_t *
svn_diff__parse_next_patch(svn_patch_t **patch,
                           apr_file_t *patch_file,
                           const char *eol_str,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  static const char * const minus = "--- ";
  static const char * const plus = "+++ ";
  const char *indicator;
  const char *fname;
  svn_stream_t *s;
  apr_off_t pos;
  svn_boolean_t eof, in_header;
  apr_pool_t *iterpool;

  if (apr_file_eof(patch_file) == APR_EOF)
    {
      /* No more patches here. */
      *patch = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the patch's filename. */
  SVN_ERR(svn_io_file_name_get(&fname, patch_file, result_pool));

  /* Get current seek position -- APR has no ftell() :( */
  pos = 0;
  apr_file_seek(patch_file, APR_CUR, &pos);

  /* Record what we already know about the patch. */
  *patch = apr_pcalloc(result_pool, sizeof(**patch));
  (*patch)->patch_file = patch_file;
  (*patch)->eol_str = eol_str;
  (*patch)->path = fname;

  /* Get a stream to read lines from the patch file.
   * The file should not be closed when we close the stream so
   * make sure it is disowned. */
  s = svn_stream_from_aprfile2(patch_file, TRUE, scratch_pool);

  indicator = minus;
  in_header = FALSE;
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;

      svn_pool_clear(iterpool);

      /* Read a line from the stream. */
      SVN_ERR(svn_stream_readline(s, &line, eol_str, &eof, iterpool));

      /* See if we have a diff header. */
      if (line->len > strlen(indicator) && starts_with(line->data, indicator))
        {
          const char *utf8_path;
          const char *canon_path;

          /* If we can find a tab, it separates the filename from
           * the rest of the line which we can discard. */
          apr_size_t tab = svn_stringbuf_find_char_backward(line, '\t');
          if (tab < line->len)
            line->data[tab] = '\0';

          /* Grab the filename and encode it in UTF-8. */
          /* TODO: Allow specifying the patch file's encoding.
           *       For now, we assume its encoding is native. */
          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_path,
                                          line->data + strlen(indicator),
                                          iterpool));

          /* Canonicalize the path name. */
          canon_path = svn_dirent_canonicalize(utf8_path, iterpool);

          if ((! in_header) && strcmp(indicator, minus) == 0)
            {
              /* First line of header contains old filename. */
              (*patch)->old_filename = apr_pstrdup(result_pool, canon_path);
              indicator = plus;
              in_header = TRUE;
            }
          else if (in_header && strcmp(indicator, plus) == 0)
            {
              /* Second line of header contains new filename. */
              (*patch)->new_filename = apr_pstrdup(result_pool, canon_path);
              in_header = FALSE;
              break; /* All good! */
            }
          else
            in_header = FALSE;
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  if ((*patch)->old_filename == NULL || (*patch)->new_filename == NULL)
    /* Something went wrong, just discard the result. */
    *patch = NULL;

  SVN_ERR(svn_stream_close(s));

  return SVN_NO_ERROR;
}

/* Try to parse a positive number from a decimal number encoded
 * in the string NUMBER. Return parsed number in OFFSET, and return
 * TRUE if parsing was successful. */
static svn_boolean_t
parse_offset(svn_linenum_t *offset, const char *number)
{
  apr_int64_t parsed_offset;
  
  errno = 0; /* clear errno for safety */
  parsed_offset = apr_atoi64(number);
  if (errno == ERANGE || parsed_offset < 0)
    return FALSE;

  /* In case we cannot fit 64 bits into an svn_linenum_t,
   * check for overflow. */
  if (sizeof(svn_linenum_t) < sizeof(parsed_offset) &&
      parsed_offset > SVN_LINENUM_MAX_VALUE)
    return FALSE;

  *offset = parsed_offset;
  return TRUE;
}

/* Try to parse a hunk range specification from the string RANGE.
 * Return parsed information in *START and *LENGTH, and return TRUE
 * if the range parsed correctly. Note: This function may modify the
 * input value RANGE. */
static svn_boolean_t
parse_range(svn_linenum_t *start, svn_linenum_t *length, char *range)
{
  char *comma;

  if (strlen(range) == 0)
    return FALSE;

  comma = strstr(range, ",");
  if (comma)
    {
      if (strlen(comma + 1) > 0)
        {
          /* Try to parse the length. */
          if (! parse_offset(length, comma + 1))
            return FALSE;

          /* Snip off the end of the string,
           * so we can comfortably parse the line
           * number the hunk starts at. */
          *comma = '\0';
        }
       else
         /* A comma but no length? */
         return FALSE;
    }
  else
    {
      *length = 1;
    }

  /* Try to parse the line number the hunk starts at. */
  return parse_offset(start, range);
}

/* Try to parse a hunk header in string HEADER, putting parsed information
 * into HUNK. Return TRUE if the header parsed correctly.
 * Do all allocations in POOL. */
static svn_boolean_t
parse_hunk_header(const char *header, svn_hunk_t *hunk, apr_pool_t *pool)
{
  static const char * const atat = "@@";
  const char *p;
  svn_stringbuf_t *range;
  
  p = header + strlen(atat);
  if (*p != ' ')
    /* No. */
    return FALSE;
  p++;
  if (*p != '-')
    /* Nah... */
    return FALSE;
  /* OK, this may be worth allocating some memory for... */
  range = svn_stringbuf_create_ensure(31, pool);
  p++;
  while (*p && *p != ' ')
    {
      svn_stringbuf_appendbytes(range, p, 1);
      p++;
    }
  if (*p != ' ')
    /* No no no... */
    return FALSE;

  /* Try to parse the first range. */
  if (! parse_range(&hunk->original_start, &hunk->original_length, range->data))
    return FALSE;

  /* Clear the stringbuf so we can reuse it for the second range. */
  svn_stringbuf_setempty(range);
  p++;
  if (*p != '+')
    /* Eeek! */
    return FALSE;
  /* OK, this may be worth copying... */
  p++;
  while (*p && *p != ' ')
    {
      svn_stringbuf_appendbytes(range, p, 1);
      p++;
    }
  if (*p != ' ')
    /* No no no... */
    return FALSE;

  /* Check for trailing @@ */
  p++;
  if (! starts_with(p, atat))
    return FALSE;

  /* There may be stuff like C-function names after the trailing @@,
   * but we ignore that. */

  /* Try to parse the second range. */
  if (! parse_range(&hunk->modified_start, &hunk->modified_length, range->data))
    return FALSE;

  /* Hunk header is good. */
  return TRUE;
}

/* A stream line-filter which allows only original text from a hunk. */
static svn_error_t *
original_line_filter(svn_boolean_t *filtered, const char *line,
                     apr_pool_t *scratch_pool)
{
  *filtered = line[0] == '+';
  return SVN_NO_ERROR;
}

/* A stream line-filter which allows only modified text from a hunk. */
static svn_error_t *
modified_line_filter(svn_boolean_t *filtered, const char *line,
                     apr_pool_t *scratch_pool)
{
  *filtered = line[0] == '-';
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff__parse_next_hunk(svn_hunk_t **hunk,
                          svn_patch_t *patch,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  static const char * const minus = "--- ";
  static const char * const atat = "@@";
  svn_boolean_t eof, in_hunk, hunk_seen;
  apr_off_t pos, last_line;
  apr_off_t start, end;
  svn_stream_t *diff_text;
  svn_stream_t *original_text;
  svn_stream_t *modified_text;
  svn_stream_t *s;
  apr_pool_t *iterpool;

  if (apr_file_eof(patch->patch_file) == APR_EOF)
    {
      /* No more hunks here. */
      *hunk = NULL;
      return SVN_NO_ERROR;
    }

  in_hunk = FALSE;
  hunk_seen = FALSE;
  *hunk = apr_pcalloc(result_pool, sizeof(**hunk));

  /* Get current seek position -- APR has no ftell() :( */
  pos = 0;
  apr_file_seek(patch->patch_file, APR_CUR, &pos);

  /* Get a stream to read lines from the patch file.
   * The file should not be closed when we close the stream so
   * make sure it is disowned. */
  s = svn_stream_from_aprfile2(patch->patch_file, TRUE, scratch_pool);

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_stringbuf_t *line;

      svn_pool_clear(iterpool);

      /* Remember the current line's offset, and read the line. */
      last_line = pos;
      SVN_ERR(svn_stream_readline(s, &line, patch->eol_str, &eof, iterpool));
      if (! eof)
        {
          /* Update line offset for next iteration.
           * APR has no ftell() :( */
          pos = 0;
          apr_file_seek(patch->patch_file, APR_CUR, &pos);
        }

      if (in_hunk)
        {
          char c;

          if (! hunk_seen)
            {
              /* We're reading the first line of the hunk, so the start
               * of the line just read is the hunk text's byte offset. */
              start = last_line;
            }
          
          c = line->data[0];
          if (c == ' ' || c == '-' || c == '+')
            hunk_seen = TRUE;
          else
            {
              in_hunk = FALSE;

              /* The start of the current line marks the first byte
               * after the hunk text. */
              end = last_line;

              break; /* Hunk was empty or has been read. */
            }
        }
      else
        { 
          if (starts_with(line->data, atat))
            /* Looks like we have a hunk header, let's try to rip it apart. */
            in_hunk = parse_hunk_header(line->data, *hunk, iterpool);
          else if (starts_with(line->data, minus))
            /* This could be a header of another patch. Bail out. */
            break;
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_stream_close(s));

  if (! eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * to this function or svn_diff__parse_next_patch() don't end
     * up skipping the line -- it may contain a patch or hunk header. */
    apr_file_seek(patch->patch_file, APR_SET, &last_line);

  if (hunk_seen && start < end)
    {
      apr_file_t *f;
      apr_int32_t flags = APR_READ | APR_BUFFERED;

      /* Create a stream which returns the hunk text itself. */
      SVN_ERR(svn_io_file_open(&f, patch->path, flags, APR_OS_DEFAULT,
                               result_pool));
      diff_text = svn_stream_from_aprfile_range_readonly(f, FALSE,
                                                         start, end,
                                                         result_pool);

      /* Create a stream which returns the original hunk text. */
      SVN_ERR(svn_io_file_open(&f, patch->path, flags, APR_OS_DEFAULT,
                               result_pool));
      original_text = svn_stream_from_aprfile_range_readonly(f, FALSE,
                                                             start, end,
                                                             result_pool);
      svn_stream_set_line_filter_callback(original_text, original_line_filter);

      /* Create a stream which returns the modified hunk text. */
      SVN_ERR(svn_io_file_open(&f, patch->path, flags, APR_OS_DEFAULT,
                               result_pool));
      modified_text = svn_stream_from_aprfile_range_readonly(f, FALSE,
                                                             start, end,
                                                             result_pool);
      svn_stream_set_line_filter_callback(modified_text, modified_line_filter);

      /* Set the hunk's texts. */
      (*hunk)->diff_text = diff_text;
      (*hunk)->original_text = original_text;
      (*hunk)->modified_text = modified_text;
    }
  else
    /* Something went wrong, just discard the result. */
    *hunk = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff__destroy_hunk(svn_hunk_t *hunk)
{
  SVN_ERR(svn_stream_close(hunk->original_text));
  SVN_ERR(svn_stream_close(hunk->modified_text));
  SVN_ERR(svn_stream_close(hunk->diff_text));
  return SVN_NO_ERROR;
}
