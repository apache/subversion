/*
 * patch.c: svnpatch related functions
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

#include <apr_lib.h>
#include <apr_pools.h>
#include <apr_errno.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_ra_svn.h"
#include "private/svn_patch.h"

#include "svn_private_config.h"


/* --- SVNPATCH ROUTINES --- */

#define svn_iswhitespace(c) ((c) == ' ' || (c) == '\n')

/* Append a tuple into TARGET in a printf-like fashion.
   See svn_ra_svn_write_tuple() for further details with the format. */
static svn_error_t *
write_tuple(svn_stream_t *target,
            apr_pool_t *pool,
            const char *fmt,
            ...);

/* --- WRITING DATA ITEMS --- */

/* Append NUMBER into TARGET stream. */
static svn_error_t *
write_number(svn_stream_t *target,
             apr_pool_t *pool,
             apr_uint64_t number)
{
  return svn_stream_printf(target, pool, "%" APR_UINT64_T_FMT " ",
                           number);
}

/* Append STR into TARGET stream.  Is binary-able. */
static svn_error_t *
write_string(svn_stream_t *target,
             apr_pool_t *pool,
             const svn_string_t *str)
{
  apr_size_t len;
  /* @c svn_stream_printf() doesn't support binary bytes.  Since
   * str->data might contain binary stuff, let's use svn_stream_write()
   * instead. */
  SVN_ERR(svn_stream_printf(target, pool, "%" APR_SIZE_T_FMT ":",
                            str->len));
  len = str->len;
  SVN_ERR(svn_stream_write(target, str->data, &len));
  SVN_ERR(svn_stream_printf(target, pool, "%s", " "));
  return SVN_NO_ERROR;
}

/* Append S cstring into TARGET stream. */
static svn_error_t *
write_cstring(svn_stream_t *target,
              apr_pool_t *pool,
              const char *s)
{
  return svn_stream_printf(target, pool, "%" APR_SIZE_T_FMT ":%s ",
                           strlen(s), s);
}

/* Append WORD into TARGET stream. */
static svn_error_t *
write_word(svn_stream_t *target,
           apr_pool_t *pool,
           const char *word)
{
  return svn_stream_printf(target, pool, "%s ", word);
}

/* Append a list of properties PROPS into TARGET. */
static svn_error_t *
write_proplist(svn_stream_t *target,
               apr_hash_t *props,
               apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  const char *propname;
  svn_string_t *propval;

  if (props)
    {
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          propname = key;
          propval = val;
          SVN_ERR(write_tuple(target, iterpool, "cs", propname, propval));
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Begin a list, appended into TARGET */
static svn_error_t *
start_list(svn_stream_t *target)
{
  apr_size_t len = 2;
  return svn_stream_write(target, "( ", &len);
}

/* End a list, appended into TARGET */
static svn_error_t *
end_list(svn_stream_t *target)
{
  apr_size_t len = 2;
  return svn_stream_write(target, ") ", &len);
}

/* --- WRITING TUPLES --- */

static svn_error_t *
vwrite_tuple(svn_stream_t *target,
             apr_pool_t *pool,
             const char *fmt,
             va_list ap)
{
  svn_boolean_t opt = FALSE;
  svn_revnum_t rev;
  const char *cstr;
  const svn_string_t *str;

  if (*fmt == '!')
    fmt++;
  else
    SVN_ERR(start_list(target));
  for (; *fmt; fmt++)
    {
      if (*fmt == 'n' && !opt)
        SVN_ERR(write_number(target, pool, va_arg(ap, apr_uint64_t)));
      else if (*fmt == 'r')
        {
          rev = va_arg(ap, svn_revnum_t);
          SVN_ERR_ASSERT(opt || SVN_IS_VALID_REVNUM(rev));
          if (SVN_IS_VALID_REVNUM(rev))
            SVN_ERR(write_number(target, pool, rev));
        }
      else if (*fmt == 's')
        {
          str = va_arg(ap, const svn_string_t *);
          SVN_ERR_ASSERT(opt || str);
          if (str)
            SVN_ERR(write_string(target, pool, str));
        }
      else if (*fmt == 'c')
        {
          cstr = va_arg(ap, const char *);
          SVN_ERR_ASSERT(opt || cstr);
          if (cstr)
            SVN_ERR(write_cstring(target, pool, cstr));
        }
      else if (*fmt == 'w')
        {
          cstr = va_arg(ap, const char *);
          SVN_ERR_ASSERT(opt || cstr);
          if (cstr)
            SVN_ERR(write_word(target, pool, cstr));
        }
      else if (*fmt == 'b' && !opt)
        {
          cstr = va_arg(ap, svn_boolean_t) ? "true" : "false";
          SVN_ERR(write_word(target, pool, cstr));
        }
      else if (*fmt == '?')
        opt = TRUE;
      else if (*fmt == '(' && !opt)
        SVN_ERR(start_list(target));
      else if (*fmt == ')')
        {
          SVN_ERR(end_list(target));
          opt = FALSE;
        }
      else if (*fmt == '!' && !*(fmt + 1))
        return SVN_NO_ERROR;
      else
        abort();
    }
  SVN_ERR(end_list(target));
  return SVN_NO_ERROR;
}

/* Append a tuple into TARGET in a printf-like fashion.
   See svn_ra_svn_write_tuple() for further details with the format. */
static svn_error_t *
write_tuple(svn_stream_t *target,
            apr_pool_t *pool,
            const char *fmt,
            ...)
{
  va_list ap;
  svn_error_t *err;

  va_start(ap, fmt);
  err = vwrite_tuple(target, pool, fmt, ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_patch__write_cmd(svn_stream_t *target,
                     apr_pool_t *pool,
                     const char *cmdname,
                     const char *fmt,
                     ...)
{
  va_list ap;
  svn_error_t *err;

  SVN_ERR(start_list(target));
  SVN_ERR(write_word(target, pool, cmdname));
  va_start(ap, fmt);
  err = vwrite_tuple(target, pool, fmt, ap);
  va_end(ap);
  if (err)
    return err;
  SVN_ERR(end_list(target));

  return SVN_NO_ERROR;
}

static svn_error_t *
readbuf_getchar_skip_whitespace(svn_stream_t *from,
                                char *result)
{
  apr_size_t lenone = 1;
  do
    SVN_ERR(svn_stream_read(from, result, &lenone));
  while (svn_iswhitespace(*result));
  return SVN_NO_ERROR;
}

static svn_error_t *
read_string(svn_stream_t *from,
            apr_pool_t *pool,
            svn_ra_svn_item_t *item,
            apr_uint64_t len)
{
  char readbuf[4096];
  apr_size_t readbuf_len;
  svn_stringbuf_t *stringbuf = svn_stringbuf_create("", pool);

  /* We can't store strings longer than the maximum size of apr_size_t,
   * so check for wrapping */
  if (((apr_size_t) len) < len)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("String length larger than maximum"));

  while (len)
    {
      readbuf_len = len > sizeof(readbuf) ? sizeof(readbuf) : len;

      SVN_ERR(svn_stream_read(from, readbuf, &readbuf_len));
      /* Read into a stringbuf_t to so we don't allow the sender to allocate
       * an arbitrary amount of memory without actually sending us that much
       * data */
      svn_stringbuf_appendbytes(stringbuf, readbuf, readbuf_len);
      len -= readbuf_len;
    }

  item->kind = SVN_RA_SVN_STRING;
  item->u.string = apr_palloc(pool, sizeof(*item->u.string));
  item->u.string->data = stringbuf->data;
  item->u.string->len = stringbuf->len;

  return SVN_NO_ERROR;
}


static svn_error_t *
read_item_internal(svn_stream_t *from,
                   apr_pool_t *pool,
                   svn_ra_svn_item_t *item,
                   char first_char,
                   int level)
{
  char c = first_char;
  apr_uint64_t val, prev_val=0;
  svn_stringbuf_t *str;
  svn_ra_svn_item_t *listitem;
  apr_size_t lenone = 1;

  if (++level >= 64)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Too many nested items"));


  /* Determine the item type and read it in.  Make sure that c is the
   * first character at the end of the item so we can test to make
   * sure it's whitespace. */
  if (apr_isdigit(c))
    {
      /* It's a number or a string.  Read the number part, either way. */
      val = c - '0';
      while (1)
        {
          prev_val = val;
          SVN_ERR(svn_stream_read(from, &c, &lenone));
          if (!apr_isdigit(c))
            break;
          val = val * 10 + (c - '0');
          if ((val / 10) != prev_val) /* val wrapped past maximum value */
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Number is larger than maximum"));
        }
      if (c == ':')
        {
          /* It's a string. */
          SVN_ERR(read_string(from, pool, item, val));
          SVN_ERR(svn_stream_read(from, &c, &lenone));
        }
      else
        {
          /* It's a number. */
          item->kind = SVN_RA_SVN_NUMBER;
          item->u.number = val;
        }
    }
  else if (apr_isalpha(c))
    {
      /* It's a word. */
      str = svn_stringbuf_ncreate(&c, 1, pool);
      while (1)
        {
          SVN_ERR(svn_stream_read(from, &c, &lenone));
          if (!apr_isalnum(c) && c != '-')
            break;
          svn_stringbuf_appendbytes(str, &c, 1);
        }
      item->kind = SVN_RA_SVN_WORD;
      item->u.word = str->data;
    }
  else if (c == '(')
    {
      /* Read in the list items. */
      item->kind = SVN_RA_SVN_LIST;
      item->u.list = apr_array_make(pool, 0, sizeof(svn_ra_svn_item_t));
      while (1)
        {
          SVN_ERR(readbuf_getchar_skip_whitespace(from, &c));
          if (c == ')')
            break;
          listitem = apr_array_push(item->u.list);
          SVN_ERR(read_item_internal(from, pool, listitem, c, level));
        }
      SVN_ERR(svn_stream_read(from, &c, &lenone));
    }

  if (!svn_iswhitespace(c))
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  return SVN_NO_ERROR;
}

static svn_error_t *
read_item(svn_stream_t *from,
          apr_pool_t *pool,
          svn_ra_svn_item_t **item)
{
  char c;

  /* Allocate space, read the first character, and then do the rest of
   * the work.  This makes sense because of the way lists are read. */
  *item = apr_palloc(pool, sizeof(**item));
  SVN_ERR(readbuf_getchar_skip_whitespace(from, &c));
  return read_item_internal(from, pool, *item, c, 0);
}

/* Parse a tuple of svn_ra_svn_item_t *'s.  Advance *FMT to the end of the
 * tuple specification and advance AP by the corresponding arguments. */
static svn_error_t *
vparse_tuple(apr_array_header_t *items,
             apr_pool_t *pool,
             const char **fmt,
             va_list *ap)
{
  int count, nesting_level;
  svn_ra_svn_item_t *elt;

  for (count = 0; **fmt && count < items->nelts; (*fmt)++, count++)
    {
      /* '?' just means the tuple may stop; skip past it. */
      if (**fmt == '?')
        (*fmt)++;
      elt = &APR_ARRAY_IDX(items, count, svn_ra_svn_item_t);
      if (**fmt == 'n' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, apr_uint64_t *) = elt->u.number;
      else if (**fmt == 'r' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, svn_revnum_t *) = elt->u.number;
      else if (**fmt == 's' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, svn_string_t **) = elt->u.string;
      else if (**fmt == 'c' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, const char **) = elt->u.string->data;
      else if (**fmt == 'w' && elt->kind == SVN_RA_SVN_WORD)
        *va_arg(*ap, const char **) = elt->u.word;
      else if (**fmt == 'b' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (strcmp(elt->u.word, "true") == 0)
            *va_arg(*ap, svn_boolean_t *) = TRUE;
          else if (strcmp(elt->u.word, "false") == 0)
            *va_arg(*ap, svn_boolean_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == 'B' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (strcmp(elt->u.word, "true") == 0)
            *va_arg(*ap, apr_uint64_t *) = TRUE;
          else if (strcmp(elt->u.word, "false") == 0)
            *va_arg(*ap, apr_uint64_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == 'l' && elt->kind == SVN_RA_SVN_LIST)
        *va_arg(*ap, apr_array_header_t **) = elt->u.list;
      else if (**fmt == '(' && elt->kind == SVN_RA_SVN_LIST)
        {
          (*fmt)++;
          SVN_ERR(vparse_tuple(elt->u.list, pool, fmt, ap));
        }
      else if (**fmt == ')')
        return SVN_NO_ERROR;
      else
        break;
    }
  if (**fmt == '?')
    {
      nesting_level = 0;
      for (; **fmt; (*fmt)++)
        {
          switch (**fmt)
            {
            case '?':
              break;
            case 'r':
              *va_arg(*ap, svn_revnum_t *) = SVN_INVALID_REVNUM;
              break;
            case 's':
              *va_arg(*ap, svn_string_t **) = NULL;
              break;
            case 'c':
            case 'w':
              *va_arg(*ap, const char **) = NULL;
              break;
            case 'l':
              *va_arg(*ap, apr_array_header_t **) = NULL;
              break;
            case 'B':
            case 'n':
              *va_arg(*ap, apr_uint64_t *) = SVN_RA_SVN_UNSPECIFIED_NUMBER;
              break;
            case '(':
              nesting_level++;
              break;
            case ')':
              if (--nesting_level < 0)
                return SVN_NO_ERROR;
              break;
            default:
              abort();
            }
        }
    }
  if (**fmt && **fmt != ')')
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_patch__parse_tuple(apr_array_header_t *list,
                       apr_pool_t *pool,
                       const char *fmt,
                       ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vparse_tuple(list, pool, &fmt, &ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_patch__read_tuple(svn_stream_t *from,
                      apr_pool_t *pool,
                      const char *fmt,
                      ...)
{
  va_list ap;
  svn_ra_svn_item_t *item;
  svn_error_t *err;

  SVN_ERR(read_item(from, pool, &item));
  if (item->kind != SVN_RA_SVN_LIST)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed patch data"));
  va_start(ap, fmt);
  err = vparse_tuple(item->u.list, pool, &fmt, &ap);
  va_end(ap);
  return err;
}

/* Functions for parsing unidiffs */

svn_error_t *
svn_patch__get_next_patch(svn_patch_t **patch,
                          apr_file_t *patch_file,
                          const char *eol_str,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  static const char *minus = "--- ";
  static const char *plus = "+++ ";
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
      if (line->len > strlen(indicator) &&
          strncmp(line->data, indicator, strlen(indicator)) == 0)
        {
          /* Looks like it, try to find the filename. */
          apr_size_t tab = svn_stringbuf_find_char_backward(line, '\t');
          if (tab >= line->len)
            /* Not found... */
            continue;

          line->data[tab] = '\0';

          if ((! in_header) && strcmp(indicator, minus) == 0)
            {
              /* First line of header. */
              (*patch)->old_filename =
                apr_pstrdup(result_pool, line->data + strlen(indicator));
              indicator = plus;
              in_header = TRUE;
            }
          else if (in_header && strcmp(indicator, plus) == 0)
            {
              /* Second line of header. */
              (*patch)->new_filename =
                apr_pstrdup(result_pool, line->data + strlen(indicator));
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

svn_error_t *
svn_patch__get_next_hunk(svn_hunk_t **hunk,
                         svn_patch_t *patch,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  static const char *atat = "@@";
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
              svn_stringbuf_appendbytes(diff_text, patch->eol_str,
                                        strlen(patch->eol_str));

              /* Grab original/modified texts. */
              switch (c)
                {
                  case ' ':
                    /* Line occurs in both. */
                    svn_stringbuf_appendbytes(original_text, line->data + 1,
                                              line->len - 1);
                    svn_stringbuf_appendbytes(original_text, patch->eol_str,
                                              strlen(patch->eol_str));
                    svn_stringbuf_appendbytes(modified_text, line->data + 1,
                                              line->len - 1);
                    svn_stringbuf_appendbytes(modified_text, patch->eol_str,
                                              strlen(patch->eol_str));
                    break;
                  case '-':
                    /* Line occurs in original. */
                    svn_stringbuf_appendbytes(original_text, line->data + 1,
                                              line->len - 1);
                    svn_stringbuf_appendbytes(original_text, patch->eol_str,
                                              strlen(patch->eol_str));
                    break;
                  case '+':
                    /* Line occurs in modified. */
                    svn_stringbuf_appendbytes(modified_text, line->data + 1,
                                              line->len - 1);
                    svn_stringbuf_appendbytes(modified_text, patch->eol_str,
                                              strlen(patch->eol_str));
                    break;
                }
            }
          else
            {
              in_hunk = FALSE;
              break; /* Hunk was empty or has been read. */
            }
        }
      else if ((! in_hunk) && strncmp(line->data, atat, strlen(atat)) == 0)
        {
          /* Looks like we have a hunk header, let's try to rip it apart. */
          char *p;
          svn_stringbuf_t *range;
          
          p = line->data + strlen(atat);
          if (*p != ' ')
            /* No. */
            continue;
          p++;
          if (*p != '-')
            /* Nah... */
            continue;
          /* OK, this may be worth allocating some memory for... */
          range = svn_stringbuf_create_ensure(31, iterpool);
          p++;
          while (*p && *p != ' ')
            {
              svn_stringbuf_appendbytes(range, p, 1);
              p++;
            }
          if (*p != ' ')
            /* No no no... */
            continue;

          /* Try to parse the first range. */
          if (! parse_range(&(*hunk)->original_start, &(*hunk)->original_length,
                            range->data))
            continue;

          /* Clear the stringbuf so we can reuse it for the second range. */
          svn_stringbuf_setempty(range);
          p++;
          if (*p != '+')
            /* Eeek! */
            continue;
          /* OK, this may be worth copying... */
          p++;
          while (*p && *p != ' ')
            {
              svn_stringbuf_appendbytes(range, p, 1);
              p++;
            }
          if (*p != ' ')
            /* No no no... */
            continue;

          /* Check for trailing @@ */
          p++;
          if (strncmp(p, atat, strlen(atat)) != 0)
            continue;

          /* There may be stuff like C-function names after the trailing @@,
           * but we ignore that. */

          /* Try to parse the second range. */
          if (! parse_range(&(*hunk)->modified_start, &(*hunk)->modified_length,
                            range->data))
            continue;

          /* Hunk header is good. */
          in_hunk = TRUE;
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_stream_close(s));

  if (! eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * to this function or svn_patch__get_next_patch() don't end
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
