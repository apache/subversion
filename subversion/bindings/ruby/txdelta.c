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
#include <svn_string.h>
#include <svn_io.h>
#include <svn_delta.h>

#include "svn_ruby.h"
#include "txdelta.h"
#include "stream.h"
#include "error.h"

static VALUE cSvnTextDelta;
static VALUE cSvnTextDeltaStream;
static VALUE cSvnTextDeltaWindow;

typedef struct svn_ruby_txdelta_t
{
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_txdelta_t;

typedef struct svn_ruby_txdelta_window_t
{
  svn_txdelta_window_t *window;
  /* When GC is happening, you can't get instance variable.  Record it
     here. */
  VALUE stream;
  apr_pool_t *pool;
} svn_ruby_txdelta_window_t;



static void
free_txdelta (void *p)
{
  svn_ruby_txdelta_t *delta = p;

  svn_pool_destroy (delta->pool);

  free (delta);
}

VALUE
svn_ruby_txdelta_new (svn_txdelta_window_handler_t handler,
                      void *handler_baton,
                      apr_pool_t *pool)
{
  svn_ruby_txdelta_t *delta;
  VALUE obj;

  obj = Data_Make_Struct (cSvnTextDelta, svn_ruby_txdelta_t,
                          0, free_txdelta, delta);
  delta->handler = handler;
  delta->handler_baton = handler_baton;
  delta->pool = pool;
  delta->closed = FALSE;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}

static VALUE
txdelta_new (VALUE class, VALUE source, VALUE target)
{
  svn_ruby_txdelta_t *delta;
  VALUE obj, argv[2];

  obj = Data_Make_Struct (class, svn_ruby_txdelta_t,
                          0, free_txdelta, delta);
  delta->pool = svn_pool_create (NULL);
  delta->closed = TRUE;
  /* svn_ruby_stream can raise exception */
  svn_txdelta_apply (svn_ruby_stream (source), svn_ruby_stream (target),
                     delta->pool, &delta->handler, &delta->handler_baton);
  delta->closed = FALSE;
  argv[0] = source;
  argv[1] = target;
  rb_obj_call_init (obj, 2, argv);

  return obj;
}

static VALUE
txdelta_init (int argc, VALUE *argv, VALUE self)
{
  VALUE source, target;

  rb_scan_args (argc, argv, "02", &source, &target);
  rb_iv_set (self, "@source", source);
  rb_iv_set (self, "@target", target);

  return self;
}

static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_baton)
{
  VALUE self = (VALUE) handler_baton;

  svn_ruby_txdelta_t *handler;
  Data_Get_Struct (self, svn_ruby_txdelta_t, handler);

  SVN_ERR (handler->handler (window, handler->handler_baton));

  if (window == NULL)
    handler->closed = TRUE;

  return SVN_NO_ERROR;
}

void
svn_ruby_txdelta (VALUE txdelta,
                  svn_txdelta_window_handler_t *handler,
                  void **baton)
{
  if (CLASS_OF (txdelta) != cSvnTextDelta)
    {
      *handler = NULL;
      *baton = NULL;
    }
  else
    {
      *handler = window_handler;
      *baton = (void *)txdelta;
    }
}


static void
closed_txdelta_error (void)
{
  /* #### */
  rb_raise (rb_eIOError, "closed delta handler");
}

static VALUE
send_string (VALUE self, VALUE aStr)
{
  svn_string_t *string;
  apr_pool_t *pool;
  svn_ruby_txdelta_t *delta;

  Data_Get_Struct (self, svn_ruby_txdelta_t, delta);

  if (delta->closed)
    closed_txdelta_error ();

  Check_Type (aStr, T_STRING);

  pool = svn_pool_create (delta->pool);

  string = svn_string_create (StringValuePtr (aStr), pool);

  SVN_RB_ERR (svn_txdelta_send_string (string, delta->handler,
                                       delta->handler_baton, pool),
              pool);

  svn_pool_destroy (pool);

  delta->closed = TRUE;

  return Qnil;
}

static VALUE
send_stream (VALUE self, VALUE aStream)
{
  svn_stream_t *stream;
  apr_pool_t *pool;
  svn_ruby_txdelta_t *delta;

  Data_Get_Struct (self, svn_ruby_txdelta_t, delta);

  if (delta->closed)
    closed_txdelta_error ();

  stream = svn_ruby_stream (aStream);

  pool = svn_pool_create (delta->pool);

  SVN_RB_ERR (svn_txdelta_send_stream (stream, delta->handler,
                                       delta->handler_baton, pool),
              pool);

  svn_pool_destroy (pool);

  delta->closed = TRUE;

  return Qnil;
}

static VALUE
apply (VALUE self, VALUE aWindow)
{
  svn_ruby_txdelta_t *handler;
  svn_error_t *err;

  if (aWindow != Qnil
      && CLASS_OF (aWindow) != cSvnTextDeltaWindow)
    rb_raise (rb_eRuntimeError,
              "Wrong argument: Window must be Svn::TextDeltaWindow");

  Data_Get_Struct (self, svn_ruby_txdelta_t, handler);

  if (handler->closed)
    closed_txdelta_error ();

  if (aWindow == Qnil)
    {
      err = handler->handler (0, handler->handler_baton);
      handler->closed = TRUE;
    }
  else
    {
      svn_ruby_txdelta_window_t *window;

      Data_Get_Struct (aWindow, svn_ruby_txdelta_window_t, window);
      err = handler->handler (window->window, handler->handler_baton);
    }

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
close (VALUE self)
{
  svn_ruby_txdelta_t *delta;

  Data_Get_Struct (self, svn_ruby_txdelta_t, delta);

  if (delta->closed)
    closed_txdelta_error ();
  else
    delta->handler (0, delta->handler_baton);

  delta->closed = TRUE;

  return Qnil;
}



/* TextDeltaWindow */

static void
mark_txdelta_window (void *p)
{
  svn_ruby_txdelta_window_t *window = p;
  rb_gc_mark (window->stream);
}

static void
free_txdelta_window (void *p)
{
  svn_ruby_txdelta_window_t *window = p;

  svn_pool_destroy (window->pool);

  free (window);
}


/* TextDeltaStream */

typedef struct svn_ruby_txdelta_stream_t
{
  svn_txdelta_stream_t *stream;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_txdelta_stream_t;

static void
free_txdelta_stream (void *p)
{
  svn_ruby_txdelta_stream_t *stream = p;

  svn_pool_destroy (stream->pool);

  free (stream);
}

static VALUE
txdelta_stream_new (VALUE class, VALUE source, VALUE target)
{
  VALUE obj;
  svn_ruby_txdelta_stream_t *stream;

  obj = Data_Make_Struct (class, svn_ruby_txdelta_stream_t,
                          0, free_txdelta_stream, stream);

  stream->pool = svn_pool_create (NULL);
  stream->closed = TRUE;

  /* svn_ruby_stream can raise exception. */
  svn_txdelta (&stream->stream,
               svn_ruby_stream (source), svn_ruby_stream (target),
               stream->pool);

  stream->closed = FALSE;

  rb_iv_set (obj, "@source", source);
  rb_iv_set (obj, "@target", target);

  rb_obj_call_init (obj, 0, 0);

  return obj;
}

static VALUE
txdelta_stream_init (VALUE self)
{
  return self;
}

static VALUE
txdelta_stream_close (VALUE self)
{
  svn_ruby_txdelta_stream_t *stream;

  Data_Get_Struct (self, svn_ruby_txdelta_stream_t, stream);

  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Already closed");

  stream->closed = TRUE;

  return Qnil;
}

static VALUE
txdelta_stream_next_window (VALUE self)
{
  svn_txdelta_window_t *window;
  svn_ruby_txdelta_stream_t *stream;
  apr_pool_t *pool;

  Data_Get_Struct (self, svn_ruby_txdelta_stream_t, stream);

  if (stream->closed)
    rb_raise (rb_eRuntimeError, "Already closed");

  pool = svn_pool_create (stream->pool);
  SVN_RB_ERR (svn_txdelta_next_window (&window, stream->stream, pool), NULL);

  if (window == NULL)
    return Qnil;
  else
    {
      svn_ruby_txdelta_window_t *rb_window;
      VALUE obj = Data_Make_Struct (cSvnTextDeltaWindow,
                                    svn_ruby_txdelta_window_t,
                                    mark_txdelta_window, free_txdelta_window,
                                    rb_window);
      rb_window->window = window;
      rb_window->stream = self;
      rb_window->pool = pool;
      
      return obj;
    }
}


void
svn_ruby_init_txdelta (void)
{
  cSvnTextDelta = rb_define_class_under (svn_ruby_mSvn, "TextDelta", rb_cObject);
  rb_define_singleton_method (cSvnTextDelta, "new", txdelta_new, 2);
  rb_define_method (cSvnTextDelta, "initialize", txdelta_init, -1);
  rb_define_method (cSvnTextDelta, "sendString", send_string, 1);
  rb_define_method (cSvnTextDelta, "sendStream", send_stream, 1);
  rb_define_method (cSvnTextDelta, "apply", apply, 1);
  rb_define_method (cSvnTextDelta, "close", close, 0);
  cSvnTextDeltaWindow = rb_define_class_under (svn_ruby_mSvn, "TextDeltaWindow",
                                               rb_cObject);
  rb_undef_method (CLASS_OF (cSvnTextDeltaWindow), "new");
  cSvnTextDeltaStream = rb_define_class_under (svn_ruby_mSvn, "TextDeltaStream",
                                               rb_cObject);
  rb_define_singleton_method (cSvnTextDeltaStream, "new", txdelta_stream_new, 2);
  rb_define_method (cSvnTextDeltaStream, "initialize", txdelta_stream_init, 0);
  rb_define_method (cSvnTextDeltaStream, "close", txdelta_stream_close, 0);
  rb_define_method (cSvnTextDeltaStream, "nextWindow",
                    txdelta_stream_next_window, 0);
}
