/*
 * stream.c:   svn_stream operations
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <stdio.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_file_io.h>
#include <apr_errno.h>

#include "svn_io.h"
#include "svn_error.h"
#include "svn_string.h"


struct svn_stream_t {
  void *baton;
  svn_read_fn_t read_fn;
  svn_write_fn_t write_fn;
  svn_close_fn_t close_fn;
};



/*** Generic streams. ***/

svn_stream_t *
svn_stream_create (void *baton, apr_pool_t *pool)
{
  svn_stream_t *stream;

  stream = apr_palloc (pool, sizeof (*stream));
  stream->baton = baton;
  stream->read_fn = NULL;
  stream->write_fn = NULL;
  stream->close_fn = NULL;
  return stream;
}


void
svn_stream_set_baton (svn_stream_t *stream, void *baton)
{
  stream->baton = baton;
}


void
svn_stream_set_read (svn_stream_t *stream, svn_read_fn_t read_fn)
{
  stream->read_fn = read_fn;
}


void
svn_stream_set_write (svn_stream_t *stream, svn_write_fn_t write_fn)
{
  stream->write_fn = write_fn;
}


void
svn_stream_set_close (svn_stream_t *stream, svn_close_fn_t close_fn)
{
  stream->close_fn = close_fn;
}


svn_error_t *
svn_stream_read (svn_stream_t *stream, char *buffer, apr_size_t *len)
{
  assert (stream->read_fn != NULL);
  return stream->read_fn (stream->baton, buffer, len);
}


svn_error_t *
svn_stream_write (svn_stream_t *stream, const char *data, apr_size_t *len)
{
  assert (stream->write_fn != NULL);
  return stream->write_fn (stream->baton, data, len);
}


svn_error_t *
svn_stream_close (svn_stream_t *stream)
{
  if (stream->close_fn == NULL)
    return SVN_NO_ERROR;
  return stream->close_fn (stream->baton);
}


svn_error_t *
svn_stream_printf (svn_stream_t *stream,
                   apr_pool_t *pool,
                   const char *fmt,
                   ...)
{
  const char *message;
  va_list ap;
  apr_size_t len;

  va_start (ap, fmt);
  message = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);
  
  len = strlen(message);
  return svn_stream_write (stream, message, &len);
}


svn_error_t *
svn_stream_readline (svn_stream_t *stream,
                     svn_stringbuf_t **stringbuf,
                     apr_pool_t *pool)
{
  apr_size_t numbytes;
  char c;
  svn_stringbuf_t *str = svn_stringbuf_create ("", pool);

  /* Since we're reading one character at a time, let's at least
     optimize for the 90% case.  90% of the time, we can avoid the
     stringbuf ever having to realloc() itself if we start it out at
     80 chars.  */
  svn_stringbuf_ensure (str, 80);

  while (1)
    {
      numbytes = 1;
      SVN_ERR (svn_stream_read (stream, &c, &numbytes));
      if (numbytes != 1)
        {
          /* a 'short' read means the stream has run out. */
          *stringbuf = NULL;
          return SVN_NO_ERROR;
        }

      if ((c == '\n'))
        break;

      svn_stringbuf_appendbytes (str, &c, 1);
    }
  
  *stringbuf = str;
  return SVN_NO_ERROR;
}




/*** Generic readable empty stream ***/

static svn_error_t *
read_handler_empty (void *baton, char *buffer, apr_size_t *len)
{
  *len = 0;
  return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_empty (void *baton, const char *data, apr_size_t *len)
{
  return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream_empty (apr_pool_t *pool)
{
  svn_stream_t *stream;

  stream = svn_stream_create (NULL, pool);
  svn_stream_set_read (stream, read_handler_empty);
  svn_stream_set_write (stream, write_handler_empty);
  return stream;
}



/*** Generic stream for APR files ***/
struct baton_apr {
  apr_file_t *file;
  apr_pool_t *pool;
};


static svn_error_t *
read_handler_apr (void *baton, char *buffer, apr_size_t *len)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_file_read_full (btn->file, buffer, *len, len);
  if (status && ! APR_STATUS_IS_EOF(status))
    return svn_error_create (status, NULL,
                             "read_handler_apr: error reading file");
  else
    return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_apr (void *baton, const char *data, apr_size_t *len)
{
  struct baton_apr *btn = baton;
  apr_status_t status;

  status = apr_file_write_full (btn->file, data, *len, len);
  if (status)
    return svn_error_create (status, NULL,
                             "write_handler_apr: error writing file");
  else
    return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream_from_aprfile (apr_file_t *file, apr_pool_t *pool)
{
  struct baton_apr *baton;
  svn_stream_t *stream;

  if (file == NULL)
    return svn_stream_empty(pool);
  baton = apr_palloc (pool, sizeof (*baton));
  baton->file = file;
  baton->pool = pool;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_apr);
  svn_stream_set_write (stream, write_handler_apr);
  return stream;
}



/* Miscellaneous stream functions. */
struct string_stream_baton
{
  svn_stringbuf_t *str;
  apr_size_t amt_read;
};

static svn_error_t *
read_handler_string (void *baton, char *buffer, apr_size_t *len)
{
  struct string_stream_baton *btn = baton;
  apr_size_t left_to_read = btn->str->len - btn->amt_read;

  *len = (*len > left_to_read) ? left_to_read : *len;
  memcpy (buffer, btn->str->data + btn->amt_read, *len);
  btn->amt_read += *len;
  return SVN_NO_ERROR;
}

static svn_error_t *
write_handler_string (void *baton, const char *data, apr_size_t *len)
{
  struct string_stream_baton *btn = baton;

  svn_stringbuf_appendbytes (btn->str, data, *len);
  return SVN_NO_ERROR;
}

svn_stream_t *
svn_stream_from_stringbuf (svn_stringbuf_t *str,
                           apr_pool_t *pool)
{
  svn_stream_t *stream;
  struct string_stream_baton *baton;

  if (! str)
    return svn_stream_empty (pool);

  baton = apr_palloc (pool, sizeof (*baton));
  baton->str = str;
  baton->amt_read = 0;
  stream = svn_stream_create (baton, pool);
  svn_stream_set_read (stream, read_handler_string);
  svn_stream_set_write (stream, write_handler_string);
  return stream;
}
