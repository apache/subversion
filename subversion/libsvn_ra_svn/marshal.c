/*
 * marshal.c :  Marshalling routines for Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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



#include <assert.h>
#include <stdlib.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_network_io.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra_svn.h"

#include "ra_svn.h"

#define svn_iswhitespace(c) ((c) == ' ' || (c) == '\n')

static svn_error_t *vparse_tuple(apr_array_header_t *list, apr_pool_t *pool,
                                 const char *fmt, va_list *ap);

/* --- CONNECTION INITIALIZATION --- */

svn_ra_svn_conn_t *svn_ra_svn_create_conn(apr_socket_t *sock,
                                          apr_file_t *in_file,
                                          apr_file_t *out_file,
                                          apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = apr_palloc(pool, sizeof(*conn));

  assert((sock && !in_file && !out_file) || (!sock && in_file && out_file));
  conn->sock = sock;
  conn->in_file = in_file;
  conn->out_file = out_file;
  conn->read_ptr = conn->read_buf;
  conn->read_end = conn->read_buf;
  conn->write_pos = 0;
  conn->pool = pool;
  return conn;
}

/* --- WRITE BUFFER MANAGEMENT --- */

/* Write bytes into the write buffer until either the write buffer is
 * full or we reach END. */
static const char *writebuf_push(svn_ra_svn_conn_t *conn, const char *data,
                                 const char *end)
{
  apr_size_t buflen, copylen;

  buflen = sizeof(conn->write_buf) - conn->write_pos;
  copylen = (buflen < end - data) ? buflen : end - data;
  memcpy(conn->write_buf + conn->write_pos, data, copylen);
  conn->write_pos += copylen;
  return data + copylen;
}

/* Write data to socket or output file as appropriate. */
static svn_error_t *writebuf_output(svn_ra_svn_conn_t *conn,
                                    const char *data, apr_size_t len)
{
  const char *end = data + len;
  apr_status_t status;
  apr_size_t count;

  while (data < end)
    {
      count = end - data;
      if (conn->sock)
        status = apr_send(conn->sock, data, &count);
      else
        status = apr_file_write(conn->out_file, data, &count);
      if (status)
        return svn_error_create(status, 0, NULL, "Write failure");
      data += count;
    }

  return SVN_NO_ERROR;
}

/* Write data from the write buffer out to the socket. */
static svn_error_t *writebuf_flush(svn_ra_svn_conn_t *conn)
{
  SVN_ERR(writebuf_output(conn, conn->write_buf, conn->write_pos));
  conn->write_pos = 0;
  return SVN_NO_ERROR;
}

static svn_error_t *writebuf_write(svn_ra_svn_conn_t *conn,
                                   const char *data, apr_size_t len)
{
  const char *end = data + len;

  if (conn->write_pos > 0 && conn->write_pos + len > sizeof(conn->write_buf))
    {
      /* Fill and then empty the write buffer. */
      data = writebuf_push(conn, data, end);
      SVN_ERR(writebuf_flush(conn));
    }

  if (end - data > sizeof(conn->write_buf))
    SVN_ERR(writebuf_output(conn, data, end - data));
  else
    writebuf_push(conn, data, end);
  return SVN_NO_ERROR;
}

static svn_error_t *writebuf_printf(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    const char *fmt, ...)
{
  va_list ap;
  char *str;

  va_start(ap, fmt);
  str = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);
  return writebuf_write(conn, str, strlen(str));
}

/* --- READ BUFFER MANAGEMENT --- */

/* Read bytes into DATA until either the read buffer is empty or
 * we reach END. */
static char *readbuf_drain(svn_ra_svn_conn_t *conn, char *data, char *end)
{
  apr_size_t buflen, copylen;

  buflen = conn->read_end - conn->read_ptr;
  copylen = (buflen < end - data) ? buflen : end - data;
  memcpy(data, conn->read_ptr, copylen);
  conn->read_ptr += copylen;
  return data + copylen;
}

/* Read data from socket or input file as appropriate. */
static svn_error_t *readbuf_input(svn_ra_svn_conn_t *conn, char *data,
                                  apr_size_t *len)
{
  apr_status_t status;

  if (conn->sock)
    status = apr_recv(conn->sock, data, len);
  else
    status = apr_file_read(conn->in_file, data, len);
  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_create(status, 0, NULL, "Read failure");
  if (len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, 0, NULL,
                            "Connection closed unexpectedly");
  return SVN_NO_ERROR;
}

/* Read data from the socket into the read buffer, which must be empty. */
static svn_error_t *readbuf_fill(svn_ra_svn_conn_t *conn)
{
  apr_size_t len;

  assert(conn->read_ptr == conn->read_end);
  writebuf_flush(conn);
  len = sizeof(conn->read_buf);
  SVN_ERR(readbuf_input(conn, conn->read_buf, &len));
  conn->read_ptr = conn->read_buf;
  conn->read_end = conn->read_buf + len;
  return SVN_NO_ERROR;
}

static svn_error_t *readbuf_getchar(svn_ra_svn_conn_t *conn, char *result)
{
  if (conn->read_ptr == conn->read_end)
    SVN_ERR(readbuf_fill(conn));
  *result = *conn->read_ptr++;
  return SVN_NO_ERROR;
}

static svn_error_t *readbuf_getchar_skip_whitespace(svn_ra_svn_conn_t *conn,
                                                    char *result)
{
  do
    {
      SVN_ERR(readbuf_getchar(conn, result));
    }
  while (svn_iswhitespace(*result));
  return SVN_NO_ERROR;
}

static svn_error_t *readbuf_read(svn_ra_svn_conn_t *conn,
                                 char *data, apr_size_t len)
{
  char *end = data + len;
  apr_size_t count;

  /* Copy in an appropriate amount of data from the buffer. */
  data = readbuf_drain(conn, data, end);

  /* Read large chunks directly into buffer. */
  while (end - data > sizeof(conn->read_buf))
    {
      writebuf_flush(conn);
      count = end - data;
      SVN_ERR(readbuf_input(conn, data, &count));
      data += count;
    }

  while (end > data)
    {
      /* The remaining amount to read is small; fill the buffer and
       * copy from that. */
      SVN_ERR(readbuf_fill(conn));
      data = readbuf_drain(conn, data, end);
    }

  return SVN_NO_ERROR;
}

/* --- WRITING DATA ITEMS --- */
 
svn_error_t *svn_ra_svn_write_number(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     apr_uint64_t number)
{
  /* ### An APR_UINT64_T_FMT would be nice here... as it stands, we'll
   * get malformed data for number >= 2^63. */
  return writebuf_printf(conn, pool, "%" APR_INT64_T_FMT " ", number);
}

svn_error_t *svn_ra_svn_write_string(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_string_t *str)
{
  SVN_ERR(writebuf_printf(conn, pool, "%" APR_SIZE_T_FMT ":", str->len));
  SVN_ERR(writebuf_write(conn, str->data, str->len));
  SVN_ERR(writebuf_write(conn, " ", 1));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_write_cstring(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool, const char *s)
{
  return writebuf_printf(conn, pool, "%" APR_SIZE_T_FMT ":%s ", strlen(s), s);
}

svn_error_t *svn_ra_svn_write_word(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *word)
{
  return writebuf_printf(conn, pool, "%s ", word);
}

svn_error_t *svn_ra_svn_start_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  return writebuf_write(conn, "( ", 2);
}

svn_error_t *svn_ra_svn_end_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  return writebuf_write(conn, ") ", 2);
}

svn_error_t *svn_ra_svn_flush(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  return writebuf_flush(conn);
}

/* --- WRITING TUPLES --- */

static svn_error_t *vwrite_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 const char *fmt, va_list ap)
{
  int opt = 0;
  svn_revnum_t rev;
  const char *cstr;
  const svn_string_t *str;

  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  for (; *fmt; fmt++)
    {
      if (*fmt == 'n')
        SVN_ERR(svn_ra_svn_write_number(conn, pool, va_arg(ap, apr_uint64_t)));
      else if (*fmt == 'r')
        {
          rev = va_arg(ap, svn_revnum_t);
          assert(opt > 0 || SVN_IS_VALID_REVNUM(rev));
          if (SVN_IS_VALID_REVNUM(rev))
            SVN_ERR(svn_ra_svn_write_number(conn, pool, rev));
        }
      else if (*fmt == 's')
        {
          str = va_arg(ap, const svn_string_t *);
          assert(opt > 0 || str);
          if (str)
            SVN_ERR(svn_ra_svn_write_string(conn, pool, str));
        }
      else if (*fmt == 'c')
        {
          cstr = va_arg(ap, const char *);
          assert(opt > 0 || cstr);
          if (cstr)
            SVN_ERR(svn_ra_svn_write_cstring(conn, pool, cstr));
        }
      else if (*fmt == 'w')
        {
          cstr = va_arg(ap, const char *);
          assert(opt > 0 || cstr);
          if (cstr)
            SVN_ERR(svn_ra_svn_write_word(conn, pool, cstr));
        }
      else if (*fmt == 'b')
        {
          cstr = va_arg(ap, svn_boolean_t) ? "true" : "false";
          SVN_ERR(svn_ra_svn_write_word(conn, pool, cstr));
        }
      else if (*fmt == '[')
        {
          SVN_ERR(svn_ra_svn_start_list(conn, pool));
          opt++;
        }
      else if (*fmt == ']')
        {
          SVN_ERR(svn_ra_svn_end_list(conn, pool));
          opt--;
        }
      else if (*fmt == '(')
        SVN_ERR(svn_ra_svn_start_list(conn, pool));
      else if (*fmt == ')')
        SVN_ERR(svn_ra_svn_end_list(conn, pool));
      else
        abort();
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_write_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vwrite_tuple(conn, pool, fmt, ap);
  va_end(ap);
  return err;
}

/* --- READING DATA ITEMS --- */

/* Given the first non-whitespace character FIRST_CHAR, read an item
 * into the already allocated structure ITEM. */
static svn_error_t *read_item(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                              svn_ra_svn_item_t *item, char first_char)
{
  char c = first_char, *strdata;
  apr_uint64_t val;
  svn_stringbuf_t *str;
  svn_ra_svn_item_t *listitem;

  /* Determine the item type and read it in.  Make sure that c is the
   * first character at the end of the item so we can test to make
   * sure it's whitespace. */
  if (apr_isdigit(c))
    {
      /* It's a number or a string.  Read the number part, either way. */
      val = c - '0';
      while (1)
        {
          SVN_ERR(readbuf_getchar(conn, &c));
          if (!apr_isdigit(c))
            break;
          val = val * 10 + (c - '0');
        }
      if (c == ':')
        {
          /* It's a string. */
          strdata = apr_palloc(pool, val + 1);
          SVN_ERR(readbuf_read(conn, strdata, val));
          strdata[val] = '\0';
          item->kind = SVN_RA_SVN_STRING;
          item->u.string = apr_palloc(pool, sizeof(*item->u.string));
          item->u.string->data = strdata;
          item->u.string->len = val;
          SVN_ERR(readbuf_getchar(conn, &c));
        }
      else
        {
          /* It's a number. */
          item->kind = SVN_RA_SVN_NUMBER;
          item->u.number = val;
        }
      return SVN_NO_ERROR;
    }
  else if (apr_isalpha(c))
    {
      /* It's a word. */
      str = svn_stringbuf_ncreate(&c, 1, pool);
      while (1)
        {
          SVN_ERR(readbuf_getchar(conn, &c));
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
          SVN_ERR(readbuf_getchar_skip_whitespace(conn, &c));
          if (c == ')')
            break;
          listitem = apr_array_push(item->u.list);
          SVN_ERR(read_item(conn, pool, listitem, c));
        }
      SVN_ERR(readbuf_getchar(conn, &c));
    }

  if (!svn_iswhitespace(c))
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                            "Malformed network data");
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_read_item(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  svn_ra_svn_item_t **item)
{
  char c;

  /* Allocate space, read the first character, and then do the rest of
   * the work.  This makes sense because of the way lists are read. */
  *item = apr_palloc(pool, sizeof(**item));
  SVN_ERR(readbuf_getchar_skip_whitespace(conn, &c));
  return read_item(conn, pool, *item, c);
}

/* We'll eventually need functions to read lists in a streaming
 * manner; that will probably want to tie into svn_streams, so I'll
 * write those later. */

/* --- READING AND PARSING TUPLES --- */

/* Parse an optional tuple.  Advance *FMT to the end of the optional
 * tuple specification. */
static svn_error_t *vparse_optional_tuple(apr_array_header_t *list,
                                          apr_pool_t *pool,
                                          const char **fmt, va_list *ap)
{
  const char *end, *subfmt;

  /* Find the beginning and end of the optional tuple spec. */
  (*fmt)++;
  end = strchr(*fmt, ']');
  assert(end);
  subfmt = apr_pstrmemdup(pool, *fmt, end - *fmt);
  *fmt = end;
  if (list->nelts > 0)
    SVN_ERR(vparse_tuple(list, pool, subfmt, ap));
  else
    {
      for (; *subfmt; subfmt++)
        {
          switch (*subfmt)
            {
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
            case '[':
            case ']':
              break;
            default: abort();
            }
        }
    }
  return SVN_NO_ERROR;
}

static svn_error_t *vparse_tuple(apr_array_header_t *list, apr_pool_t *pool,
                                 const char *fmt, va_list *ap)
{
  int count;
  svn_ra_svn_item_t *elt;

  for (count = 0; *fmt && count < list->nelts; fmt++, count++)
    {
      elt = &((svn_ra_svn_item_t *) list->elts)[count];
      if (*fmt == 'n' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, apr_uint64_t *) = elt->u.number;
      else if (*fmt == 'r' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, svn_revnum_t *) = elt->u.number;
      else if (*fmt == 's' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, svn_string_t **) = elt->u.string;
      else if (*fmt == 'c' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, const char **) = elt->u.string->data;
      else if (*fmt == 'w' && elt->kind == SVN_RA_SVN_WORD)
        *va_arg(*ap, const char **) = elt->u.word;
      else if (*fmt == 'b' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (strcmp(elt->u.word, "true") == 0)
            *va_arg(*ap, svn_boolean_t *) = TRUE;
          else if (strcmp(elt->u.word, "false") == 0)
            *va_arg(*ap, svn_boolean_t *) = FALSE;
          else
            break;
        }
      else if (*fmt == 'l' && elt->kind == SVN_RA_SVN_LIST)
        *va_arg(*ap, apr_array_header_t **) = elt->u.list;
      else if (*fmt == '[' && elt->kind == SVN_RA_SVN_LIST)
        SVN_ERR(vparse_optional_tuple(elt->u.list, pool, &fmt, ap));
      else
        break;
    }
  if (!*fmt)
    return SVN_NO_ERROR;
  return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                          "Malformed network data");
}

svn_error_t *svn_ra_svn_parse_tuple(apr_array_header_t *list,
                                    apr_pool_t *pool,
                                    const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vparse_tuple(list, pool, fmt, &ap);
  va_end(ap);
  return err;
}

svn_error_t *svn_ra_svn_read_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *fmt, ...)
{
  va_list ap;
  svn_ra_svn_item_t *item;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn_read_item(conn, pool, &item));
  if (item->kind != SVN_RA_SVN_LIST)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                            "Malformed network data");
  va_start(ap, fmt);
  err = vparse_tuple(item->u.list, pool, fmt, &ap);
  va_end(ap);
  return err;
}

/* --- READING AND WRITING COMMANDS AND RESPONSES --- */

svn_error_t *svn_ra_svn_read_cmd_response(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool,
                                          const char *fmt, ...)
{
  va_list ap;
  const char *status, *message, *file;
  apr_array_header_t *params;
  svn_error_t *err;
  svn_ra_svn_item_t *elt;
  int i;
  apr_uint64_t apr_err, line;

  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "wl", &status, &params));
  if (strcmp(status, "success") == 0)
    {
      va_start(ap, fmt);
      err = vparse_tuple(params, pool, fmt, &ap);
      va_end(ap);
      return err;
    }
  else if (strcmp(status, "failure") == 0)
    {
      /* Rebuild the error list from the end, to avoid reversing the order. */
      if (params->nelts == 0)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                "Empty error list");
      err = NULL;
      for (i = params->nelts - 1; i >= 0; i--)
        {
          elt = &((svn_ra_svn_item_t *) params->elts)[i];
          if (elt->kind != SVN_RA_SVN_LIST)
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                    "Malformed error list");
          SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "nccn", &apr_err,
                                          &message, &file, &line));
          err = svn_error_create(apr_err, 0, err, message);
          err->file = apr_pstrdup(err->pool, file);
          err->line = line;
        }
      return err;
    }

  return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                           "Unknown status '%s' in command response", status);
}

svn_error_t *svn_ra_svn_handle_commands(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool,
                                        svn_ra_svn_cmd_entry_t *commands,
                                        void *baton,
                                        svn_boolean_t pass_through_errors)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *cmdname;
  int i;
  svn_error_t *err;
  apr_array_header_t *params;

  while (1)
    {
      SVN_ERR(svn_ra_svn_read_tuple(conn, subpool, "wl", &cmdname, &params));
      for (i = 0; commands[i].cmdname; i++)
	{
	  if (strcmp(cmdname, commands[i].cmdname) == 0)
	    break;
	}
      if (commands[i].cmdname)
	{
	  err = (*commands[i].handler)(conn, subpool, params, baton);
	  if (err && err->apr_err == SVN_ERR_RA_SVN_CMD_ERR)
	    err = err->child;
	  else if (err)
	    return err;
	}
      else
	err = svn_error_createf(SVN_ERR_RA_SVN_UNKNOWN_CMD, 0, NULL,
				"Unknown command %s", cmdname);
      if (err)
        {
          svn_ra_svn_write_cmd_failure(conn, subpool, err);
          if (pass_through_errors)
            return err;
        }
      svn_error_clear(err);
      apr_pool_clear(subpool);

      if (commands[i].terminate)
        break;
    }
  apr_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_write_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  const char *cmdname, const char *fmt, ...)
{
  va_list ap;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, cmdname));
  va_start(ap, fmt);
  err = vwrite_tuple(conn, pool, fmt, ap);
  va_end(ap);
  if (err)
    return err;
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_write_cmd_response(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           const char *fmt, ...)
{
  va_list ap;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "success"));
  va_start(ap, fmt);
  err = vwrite_tuple(conn, pool, fmt, ap);
  va_end(ap);
  if (err)
    return err;
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_write_cmd_failure(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool, svn_error_t *err)
{
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "failure"));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  for (; err; err = err->child)
    {
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "nccn",
                                     (apr_uint64_t) err->apr_err,
                                     err->message, err->file,
                                     (apr_uint64_t) err->line));
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  return SVN_NO_ERROR;
}
