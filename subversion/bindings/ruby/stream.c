/*
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <ruby.h>
#include <svn_pools.h>
#include <svn_io.h>

#include "svn_ruby.h"
#include "stream.h"
#include "error.h"

static VALUE cSvnStream;
static VALUE cSvnEmptyReader, cSvnFileStream;

typedef struct svn_ruby_stream_t
{
  svn_stream_t *stream;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_stream_t;



static void
stream_free (void *p)
{
  svn_ruby_stream_t *stream = p;
  if (!stream->closed)
    {
      svn_stream_close (stream->stream);
      svn_pool_destroy (stream->pool);
    }
  free (stream);
}

VALUE
svn_ruby_stream_new (VALUE class,
                     svn_stream_t *stream,
                     apr_pool_t *pool)
{
  VALUE obj;
  svn_ruby_stream_t *rb_stream;
  if (class == Qnil)
    class = cSvnStream;

  obj = Data_Make_Struct (class, svn_ruby_stream_t,
                          0, stream_free, rb_stream);
  rb_stream->stream = stream;
  rb_stream->pool = pool;
  rb_stream->closed = FALSE;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}

static VALUE
read (VALUE self, VALUE aInt)
{
  char *buffer;
  apr_size_t len;
  apr_pool_t *pool;
  svn_ruby_stream_t *stream;
  VALUE obj;

  len = NUM2LONG (aInt);
  Data_Get_Struct (self, svn_ruby_stream_t, stream);
  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Stream is already closed");
  pool = svn_pool_create (stream->pool);
  buffer = apr_palloc (pool, len);

  SVN_RB_ERR (svn_stream_read (stream->stream, buffer, &len), pool);

  if (!len)
    {
      svn_pool_destroy (pool);
      return Qnil;
    }

  obj = rb_str_new (buffer, len);

  svn_pool_destroy (pool);

  return obj;
}

static VALUE
close (VALUE self)
{
  svn_ruby_stream_t *stream;

  Data_Get_Struct (self, svn_ruby_stream_t, stream);

  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Stream is already closed");

  SVN_RB_ERR (svn_stream_close (stream->stream), NULL);

  svn_pool_destroy (stream->pool);

  stream->closed = TRUE;

  return Qnil;
}

static VALUE
empty_new (VALUE class)
{
  svn_stream_t *stream;
  apr_pool_t *pool;

  pool = svn_pool_create (NULL);
  stream = svn_stream_empty (pool);

  return svn_ruby_stream_new (class, stream, pool);
}  

typedef struct svn_ruby_file_stream_t
{
  svn_stream_t *stream;
  apr_pool_t *pool;
  svn_boolean_t closed;
  apr_file_t *file;
} svn_ruby_file_stream_t;

static void
file_free (void *p)
{
  svn_ruby_file_stream_t *stream = p;
  if (!stream->closed)
    {
      svn_stream_close (stream->stream);
      apr_file_close (stream->file);
      svn_pool_destroy (stream->pool);
    }
  free (stream);
}

static VALUE
file_new (VALUE class, VALUE aPath, VALUE flag)
{
  svn_stream_t *stream;
  apr_file_t *file = NULL;
  char *path;
  apr_pool_t *pool;
  apr_status_t status;
  svn_ruby_file_stream_t *rb_stream;

  VALUE obj, argv[2];

  Check_Type (aPath, T_STRING);

  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);

  /* XXX should we be using svn_file_open here? */
  status = apr_file_open (&file, path,
                          NUM2LONG (flag), APR_OS_DEFAULT,
                          pool);
  if (status)
    svn_ruby_raise (svn_error_createf (status, 0,
                                       "Failed to open file %s",
                                       path));

  stream = svn_stream_from_aprfile (file, pool);

  obj = Data_Make_Struct (class, svn_ruby_file_stream_t,
                          0, file_free, rb_stream);

  rb_stream->stream = stream;
  rb_stream->pool = pool;
  rb_stream->closed = FALSE;
  rb_stream->file = file;

  argv[0] = aPath;
  argv[1] = flag;

  rb_obj_call_init (obj, 2, argv);

  return obj;
}

static VALUE
file_init (VALUE self, VALUE aPath, VALUE flag)
{
  return self;
}

static VALUE
file_write (VALUE self, VALUE aString)
{
  svn_ruby_stream_t *stream;
  apr_size_t len;

  Data_Get_Struct (self, svn_ruby_stream_t, stream);

  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Stream is already closed");

  Check_Type (aString, T_STRING);

  len = RSTRING (aString)->len;

  SVN_RB_ERR (svn_stream_write (stream->stream, StringValuePtr (aString), &len),
              NULL);

  return LONG2NUM (len);
}

static VALUE
file_close (VALUE self)
{
  svn_ruby_file_stream_t *stream;
  apr_status_t status;

  Data_Get_Struct (self, svn_ruby_file_stream_t, stream);

  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Stream is already closed");

  SVN_RB_ERR (svn_stream_close (stream->stream), NULL);

  status = apr_file_close (stream->file);
  if (status)
    rb_raise (rb_eRuntimeError, "failed to close file");

  svn_pool_destroy (stream->pool);

  stream->closed = TRUE;

  return Qnil;
}

svn_stream_t *
svn_ruby_stream (VALUE aStream)
{
  VALUE c;

  for (c = CLASS_OF (aStream); RCLASS (c)->super; c = RCLASS (c)->super)
    {
      if (c == cSvnStream
          || c == cSvnEmptyReader
          || c == cSvnFileStream)
        {
          svn_ruby_stream_t *stream;

          Data_Get_Struct (aStream, svn_ruby_stream_t, stream);

          return stream->stream;
        }
    }

  rb_raise (rb_eRuntimeError, "Object must be the subclass of Svn::Stream");
}

void
svn_ruby_init_stream (void)
{
  cSvnStream = rb_define_class_under (svn_ruby_mSvn, "Stream", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnStream), "new");
  rb_define_method (cSvnStream, "read", read, 1);
  rb_define_method (cSvnStream, "close", close, 0);
  cSvnEmptyReader = rb_define_class_under (svn_ruby_mSvn, "EmptyReader",
                                           cSvnStream);
  rb_define_singleton_method (cSvnEmptyReader, "new", empty_new, 0);
  cSvnFileStream = rb_define_class_under (svn_ruby_mSvn, "FileStream",
                                          cSvnStream);
  rb_define_singleton_method (cSvnFileStream, "new", file_new, 2);
  rb_define_method (cSvnFileStream, "initialize", file_init, 2);
  rb_define_const (cSvnFileStream, "READ", INT2FIX (APR_READ));
  rb_define_const (cSvnFileStream, "WRITE", INT2FIX (APR_WRITE));
  rb_define_const (cSvnFileStream, "CREATE", INT2FIX (APR_CREATE));
  rb_define_const (cSvnFileStream, "APPEND", INT2FIX (APR_APPEND));
  rb_define_const (cSvnFileStream, "TRUNCATE", INT2FIX (APR_TRUNCATE));
  rb_define_const (cSvnFileStream, "BINARY", INT2FIX (APR_BINARY));
  rb_define_const (cSvnFileStream, "EXCL", INT2FIX (APR_EXCL));
  rb_define_const (cSvnFileStream, "BUFFERED", INT2FIX (APR_BUFFERED));
  rb_define_const (cSvnFileStream, "DELONCLOSE", INT2FIX (APR_DELONCLOSE));
  rb_define_method (cSvnFileStream, "write", file_write, 1);
  rb_define_method (cSvnFileStream, "close", file_close, 0);
}
