/*
 * parse-diff.c: functions for parsing diff files
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

  /* Get current seek position -- APR has no ftell() :( */
  pos = 0;
  apr_file_seek(patch_file, APR_CUR, &pos);

  /* Record what we already know about the patch. */
  *patch = apr_pcalloc(result_pool, sizeof(**patch));
  (*patch)->patch_file = patch_file;
  (*patch)->eol_str = eol_str;
  
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
          const char *path;

          /* Looks like it, try to find the filename. */
          apr_size_t tab = svn_stringbuf_find_char_backward(line, '\t');
          if (tab >= line->len)
            /* Not found... */
            continue;

          /* Grab the filename and encode it in UTF-8. */
          /* TODO: Allow specifying the patch file's encoding.
           *       For now, we assume its encoding is native. */
          line->data[tab] = '\0';
          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_path,
                                          line->data + strlen(indicator),
                                          iterpool));

          /* Canonicalize the path name. */
          canon_path = svn_dirent_canonicalize(utf8_path, iterpool);

          /* Strip leading slash, if any. */
          if (svn_dirent_is_absolute(canon_path))
            path = canon_path + 1;
          else
            path = canon_path;

          if ((! in_header) && strcmp(indicator, minus) == 0)
            {
              /* First line of header contains old filename. */
              (*patch)->old_filename = apr_pstrdup(result_pool, path);
              indicator = plus;
              in_header = TRUE;
            }
          else if (in_header && strcmp(indicator, plus) == 0)
            {
              /* Second line of header contains new filename. */
              (*patch)->new_filename = apr_pstrdup(result_pool, path);
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
parse_offset(svn_filesize_t *offset, const char *number)
{
  apr_int64_t parsed_offset;
  
  errno = 0; /* clear errno for safety */
  parsed_offset = apr_atoi64(number);
  if (errno == ERANGE || parsed_offset < 0)
    return FALSE;

  /* svn_filesize_t is 64 bit. */
  *offset = parsed_offset;
  return TRUE;
}

/* Try to parse a hunk range specification from the string RANGE.
 * Return parsed information in *START and *LENGTH, and return TRUE
 * if the range parsed correctly. Note: This function may modify the
 * input value RANGE. */
static svn_boolean_t
parse_range(svn_filesize_t *start, svn_filesize_t *length, char *range)
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
  svn_stringbuf_t *diff_text;
  svn_stringbuf_t *original_text;
  svn_stringbuf_t *modified_text;
  svn_stream_t *s;
  apr_pool_t *iterpool;

  if (apr_file_eof(patch->patch_file) == APR_EOF)
    {
      /* No more hunks here. */
      *hunk = NULL;
      return SVN_NO_ERROR;
    }

  /* With 4096 characters, we probably won't have to realloc
   * for averagely small hunks. */
  diff_text = svn_stringbuf_create_ensure(4096, scratch_pool);
  original_text = svn_stringbuf_create_ensure(4096, scratch_pool);
  modified_text = svn_stringbuf_create_ensure(4096, scratch_pool);

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
          char c = line->data[0];
          if (c == ' ' || c == '-' || c == '+')
            {
              hunk_seen = TRUE;

              /* Every line of the hunk is part of the diff text. */
              svn_stringbuf_appendbytes(diff_text, line->data, line->len);
              svn_stringbuf_appendcstr(diff_text, patch->eol_str);

              /* Grab original/modified texts. */
              switch (c)
                {
                  case ' ':
                    /* Line occurs in both. */
                    svn_stringbuf_appendbytes(original_text,
                                              line->data + 1, line->len - 1);
                    svn_stringbuf_appendcstr(original_text, patch->eol_str);
                    svn_stringbuf_appendbytes(modified_text,
                                              line->data + 1, line->len - 1);
                    svn_stringbuf_appendcstr(modified_text, patch->eol_str);
                    break;
                  case '-':
                    /* Line occurs in original. */
                    svn_stringbuf_appendbytes(original_text,
                                              line->data + 1, line->len - 1);
                    svn_stringbuf_appendcstr(original_text, patch->eol_str);
                    break;
                  case '+':
                    /* Line occurs in modified. */
                    svn_stringbuf_appendbytes(modified_text,
                                              line->data + 1, line->len - 1);
                    svn_stringbuf_appendcstr(modified_text, patch->eol_str);
                    break;
                }
            }
          else
            {
              in_hunk = FALSE;
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

  if (hunk_seen)
    {
      /* Set the hunk's texts. */
      (*hunk)->diff_text = svn_string_create(diff_text->data, result_pool);
      (*hunk)->original_text = svn_string_create(original_text->data,
                                                 result_pool);
      (*hunk)->modified_text = svn_string_create(modified_text->data,
                                                 result_pool);
    }
  else
    /* Something went wrong, just discard the result. */
    *hunk = NULL;

  return SVN_NO_ERROR;
}
