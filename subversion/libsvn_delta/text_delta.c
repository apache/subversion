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
#include <string.h>
#include "svn_delta.h"
#include "svn_io.h"
#include "delta.h"



/* Text delta stream descriptor. */

struct svn_txdelta_stream_t {
  /* These are copied from parameters passed to svn_txdelta. */
  svn_read_fn_t *source_fn;
  void *source_baton;
  svn_read_fn_t *target_fn;
  void *target_baton;

  /* Private data */
  apr_pool_t *pool;             /* Pool to allocate stream data from. */
  svn_boolean_t more;           /* TRUE if there are more data in the pool. */
  apr_off_t pos;                /* Position in source file. */
};


/* Text delta applicator.  */

struct apply_baton {
  /* These are copied from parameters passed to svn_txdelta_apply.  */
  svn_read_fn_t *source_fn;
  void *source_baton;
  svn_write_fn_t *target_fn;
  void *target_baton;

  /* Private data.  Between calls, SBUF contains the data from the
   * last window's source view, as specified by SBUF_OFFSET and
   * SBUF_LEN.  The contents of TBUF are not interesting between
   * calls.  */
  apr_pool_t *pool;             /* Pool to allocate data from */
  char *sbuf;                   /* Source buffer */
  apr_size_t sbuf_size;         /* Allocated source buffer space */
  apr_off_t sbuf_offset;        /* Offset of SBUF data in source stream */
  apr_size_t sbuf_len;          /* Length of SBUF data */
  char *tbuf;                   /* Target buffer */
  apr_size_t tbuf_size;         /* Allocated target buffer space */
};



/* Allocate a delta window. */

svn_error_t *
svn_txdelta__init_window (svn_txdelta_window_t **window,
                          svn_txdelta_stream_t *stream)
{
  apr_pool_t *pool = svn_pool_create (stream->pool, NULL);
  assert (pool != NULL);

  (*window) = apr_palloc (pool, sizeof (**window));
  (*window)->sview_offset = 0;
  (*window)->sview_len = 0;
  (*window)->tview_len = 0;
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
  apr_pool_t *subpool = svn_pool_create (pool, NULL);
  assert (subpool != NULL);

  *stream = apr_palloc (subpool, sizeof (**stream));
  (*stream)->source_fn = source_fn; 
  (*stream)->source_baton = source_baton;
  (*stream)->target_fn = target_fn;
  (*stream)->target_baton = target_baton;
  (*stream)->pool = subpool;
  (*stream)->more = TRUE;
  (*stream)->pos = 0;
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
      apr_off_t source_offset = stream->pos;
      apr_size_t source_len = svn_txdelta__window_size;
      apr_size_t target_len = svn_txdelta__window_size;
      apr_pool_t *temp_pool = svn_pool_create (stream->pool, NULL);
      char *buffer = apr_palloc (temp_pool, source_len + target_len);

      /* Read the source and target streams. */
      stream->source_fn (stream->source_baton, buffer,
                         &source_len, NULL/*FIXME:*/);
      stream->target_fn (stream->target_baton, buffer + source_len,
                         &target_len, NULL/*FIXME:*/);
      stream->pos += source_len;

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
        {
          (*window)->sview_offset = source_offset;
          (*window)->sview_len = source_len;
          (*window)->tview_len = target_len;
          err = svn_txdelta__vdelta (*window, buffer,
                                     source_len, target_len,
                                     temp_pool);
        }

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



/* Functions for applying deltas.  */

/* Ensure that BUF has enough space for VIEW_LEN bytes.  */
static APR_INLINE void
size_buffer (char **buf, apr_size_t *buf_size,
             apr_size_t view_len, apr_pool_t *pool)
{
  if (view_len > *buf_size)
    {
      *buf_size *= 2;
      if (*buf_size < view_len)
        *buf_size = view_len;
      *buf = apr_palloc (pool, *buf_size);
    }
}


/* Apply the instructions from WINDOW to a source view SBUF to produce
   a target view TBUF.  SBUF is assumed to have WINDOW->sview_len
   bytes of data and TBUF is assumed to have room for
   WINDOW->tview_len bytes of output.  This is purely a memory
   operation; nothing can go wrong as long as we have a valid window.  */

static void
apply_instructions (svn_txdelta_window_t *window, const char *sbuf, char *tbuf)
{
  svn_txdelta_op_t *op;
  apr_size_t i, tpos = 0;

  for (op = window->ops; op < window->ops + window->num_ops; op++)
    {
      /* Check some invariants common to all instructions.  */
      assert (op->offset >= 0 && op->length >= 0);
      assert (tpos + op->length <= window->tview_len);

      switch (op->action_code)
        {
        case svn_txdelta_source:
          /* Copy from source area.  */
          assert (op->offset + op->length <= window->sview_len);
          memcpy (tbuf + tpos, sbuf + op->offset, op->length);
          tpos += op->length;
          break;

        case svn_txdelta_target:
          /* Copy from target area.  Don't use memcpy() since its
             semantics aren't guaranteed for overlapping memory areas,
             and target copies are allowed to overlap to generate
             repeated data.  */
          assert (op->offset < tpos);
          for (i = op->offset; i < op->offset + op->length; i++)
            tbuf[tpos++] = tbuf[i];
          break;

        case svn_txdelta_new:
          /* Copy from window new area.  */
          assert (op->offset + op->length <= window->new->len);
          memcpy (tbuf + tpos, window->new->data + op->offset, op->length);
          tpos += op->length;
          break;

        default:
          assert ("Invalid delta instruction code" == NULL);
        }
    }

  /* Check that we produced the right amount of data.  */
  assert (tpos == window->tview_len);
}


/* Apply WINDOW to the streams given by APPL.  */
static svn_error_t *
apply_window (svn_txdelta_window_t *window, void *baton)
{
  struct apply_baton *ab = (struct apply_baton *) baton;
  apr_size_t len;
  svn_error_t *err;

  if (window == NULL)
    {
      /* We're done; just clean up.  */
      apr_destroy_pool (ab->pool);
      return SVN_NO_ERROR;
    }

  /* Make sure the source view didn't slide backwards.  */
  assert (window->sview_offset >= ab->sbuf_offset
          && (window->sview_offset + window->sview_len
              >= ab->sbuf_offset + ab->sbuf_len));

  /* Make sure there's enough room in the target buffer.  */
  size_buffer (&ab->tbuf, &ab->tbuf_size, window->tview_len, ab->pool);

  /* Prepare the source buffer for reading from the input stream.  */
  if (window->sview_offset != ab->sbuf_offset
      || window->sview_len > ab->sbuf_size)
    {
      char *old_sbuf = ab->sbuf;

      /* Make sure there's enough room.  */
      size_buffer (&ab->sbuf, &ab->sbuf_size, window->sview_len, ab->pool);

      /* If the existing view overlaps with the new view, copy the
       * overlap to the beginning of the new buffer.  */
      if (ab->sbuf_offset + ab->sbuf_len > window->sview_offset)
        {
          apr_size_t start = window->sview_offset - ab->sbuf_offset;
          memmove (ab->sbuf, old_sbuf + start, ab->sbuf_len - start);
          ab->sbuf_len -= start;
        }
      else
        ab->sbuf_len = 0;
      ab->sbuf_offset = window->sview_offset;
    }

  /* Read the remainder of the source view into the buffer.  */
  if (ab->sbuf_len < window->sview_len)
    {
      len = window->sview_len - ab->sbuf_len;
      err = ab->source_fn (ab->source_baton, ab->sbuf + ab->sbuf_len, &len,
                           ab->pool);
      if (err == SVN_NO_ERROR && len != window->sview_len - ab->sbuf_len)
        err = svn_error_create (SVN_ERR_INCOMPLETE_DATA, 0, NULL, ab->pool,
                                "Delta source ended unexpectedly");
      if (err != SVN_NO_ERROR)
        return err;
      ab->sbuf_len = window->sview_len;
    }

  /* Apply the window instructions to the source view to generate
     the target view.  */
  apply_instructions (window, ab->sbuf, ab->tbuf);

  /* Write out the output. */
  len = window->tview_len;
  err = ab->target_fn (ab->target_baton, ab->tbuf, &len, ab->pool);
  return err;
}


svn_error_t *
svn_txdelta_apply (svn_read_fn_t *source_fn,
                   void *source_baton,
                   svn_write_fn_t *target_fn,
                   void *target_baton,
                   apr_pool_t *pool,
                   svn_txdelta_window_handler_t **handler,
                   void **handler_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool, NULL);
  struct apply_baton *ab;
  assert (pool != NULL);

  ab = apr_palloc (subpool, sizeof (*ab));
  ab->source_fn = source_fn;
  ab->source_baton = source_baton;
  ab->target_fn = target_fn;
  ab->target_baton = target_baton;
  ab->pool = subpool;
  ab->sbuf = NULL;
  ab->sbuf_size = 0;
  ab->sbuf_offset = 0;
  ab->sbuf_len = 0;
  ab->tbuf = NULL;
  ab->tbuf_size = 0;
  *handler = apply_window;
  *handler_baton = ab;
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
