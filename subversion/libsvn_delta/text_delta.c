/* 
 * text-delta.c -- Internal text delta representation
 * 
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */


#include <assert.h>
#include "svn_delta.h"
#include "delta.h"



/* Text delta stream descriotor. */

struct svn_txdelta_stream_t {
  /* These are copied from parameters passed to svn_txdelta. */
  svn_read_fn_t* source_fn;
  void* source_baton;
  svn_read_fn_t* target_fn;
  void* target_baton;

  /* Private data */
  apr_pool_t* pool;             /* Pool to allocate stream data from. */
  svn_boolean_t more;           /* TRUE if there are more data in the pool. */
};



/* Allocate a delta window. */

svn_error_t *
svn_txdelta__init_window (svn_txdelta_window_t **window,
                          svn_txdelta_stream_t *stream)
{
  apr_pool_t *pool = apr_make_sub_pool (stream->pool, NULL);
  assert (pool != NULL);

  (*window) = apr_palloc (pool, sizeof (**window));
  (*window)->num_ops = 0;
  (*window)->ops_size = 0;
  (*window)->ops = NULL;
  (*window)->new = svn_string_create ("", pool);
  (*window)->pool = pool;
  return SVN_NO_ERROR;
}



/* Insert a delta op into a delta window. */

svn_error_t *
svn_txdelta__insert_op (svn_txdelta_window_t *window,
                        int opcode,
                        apr_off_t offset,
                        apr_off_t length,
                        const char *new_data)
{
  svn_txdelta_op_t *op;

  /* Create space for the new op. */
  if (window->num_ops == window->ops_size)
    {
      svn_txdelta_op_t *const old_ops = window->ops;
      int const new_ops_size = (window->ops_size == 0
                                ? 16 : 2 * window->ops_size);
      window->ops =
        apr_palloc (window->pool, new_ops_size * sizeof (*window->ops));

      /* Copy any existing ops into the new array */
      if (old_ops)
        memcpy (window->ops, old_ops,
                window->ops_size * sizeof (*window->ops));
      window->ops_size = new_ops_size;
    }

  /* Insert the op. svn_delta_source and svn_delta_target are
     just inserted. For svn_delta_new, the new data must be
     copied into the window. */
  op = &window->ops[window->num_ops];
  switch (opcode)
    {
    case svn_txdelta_source:
    case svn_txdelta_target:
      op->action_code = opcode;
      op->offset = offset;
      op->length = length;
      break;
    case svn_txdelta_new:
      op->action_code = opcode;
      op->offset = window->new->len;
      op->length = length;
      svn_string_appendbytes (window->new, new_data, length, window->pool);
      break;
    default:
      assert (!"unknown delta op.");
    }

  ++window->num_ops;
  return SVN_NO_ERROR;
}



/* Allocate a delta stream descriptor. */

svn_error_t *
svn_txdelta (svn_txdelta_stream_t **stream,
             svn_read_fn_t *source_fn,
             void *source_baton,
             svn_read_fn_t *target_fn,
             void *target_baton,
             apr_pool_t *pool)
{
  apr_pool_t *subpool = apr_make_sub_pool (pool, NULL);
  assert (subpool != NULL);

  *stream = apr_palloc (pool, sizeof (**stream));
  (*stream)->source_fn = source_fn; 
  (*stream)->source_baton = source_baton;
  (*stream)->target_fn = target_fn;
  (*stream)->target_baton = target_baton;
  (*stream)->pool = subpool;
  (*stream)->more = TRUE;
  return SVN_NO_ERROR;
}


void
svn_txdelta_free (svn_txdelta_stream_t *stream)
{
  if (stream)
    apr_destroy_pool (stream->pool);
}



/* Pull the next delta window from a stream. */
svn_error_t *
svn_txdelta_next_window (svn_txdelta_window_t **window,
                         svn_txdelta_stream_t *stream)
{
  if (!stream->more)
    {
      *window = NULL;
      return SVN_NO_ERROR;
    }
  else
    {
      svn_error_t *err = SVN_NO_ERROR;
      apr_size_t source_len = svn_txdelta__window_size;
      apr_size_t target_len = svn_txdelta__window_size;
      apr_pool_t *temp_pool = apr_make_sub_pool (stream->pool, NULL);
      char *buffer = apr_palloc (temp_pool, source_len + target_len);

      /* Read the source and target streams. */
      stream->source_fn (stream->source_baton, buffer,
                         &source_len, NULL/*FIXME:*/);
      stream->target_fn (stream->target_baton, buffer + source_len,
                         &target_len, NULL/*FIXME:*/);

      /* Forget everything if there's no target data. */
      *window = NULL;
      if (target_len == 0)
        {
          stream->more = FALSE;
          apr_destroy_pool (temp_pool);
          return SVN_NO_ERROR;
        }

      /* Create the delta window */
      err = svn_txdelta__init_window (window, stream);
      if (err == SVN_NO_ERROR)
        err = svn_txdelta__vdelta (*window, buffer,
                                   source_len, target_len,
                                   temp_pool);

      /* That's it. */
      apr_destroy_pool (temp_pool);
      if (err)
        {
          svn_txdelta_free_window (*window);
          *window = NULL;
        }
      return err;
    }
}


void
svn_txdelta_free_window (svn_txdelta_window_t *window)
{
  if (window)
    apr_destroy_pool (window->pool);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
