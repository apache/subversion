/*
 * patch.c: svnpatch related functions
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

#include <apr_lib.h>
#include <apr_pools.h>

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

#if 0 /* NOTUSED */
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
#endif

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
