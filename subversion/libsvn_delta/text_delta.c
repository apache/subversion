/* 
 * text-delta.c -- Internal text delta representation
 * 
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


#include <assert.h>
#include <string.h>

#include <apr_general.h>        /* for APR_INLINE */
#include <apr_md5.h>            /* for, um...MD5 stuff */

#include "svn_delta.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "delta.h"


/* Text delta stream descriptor. */

struct svn_txdelta_stream_t {
  /* These are copied from parameters passed to svn_txdelta. */
  svn_stream_t *source;
  svn_stream_t *target;

  /* Private data */
  svn_boolean_t more;           /* TRUE if there are more data in the pool. */
  apr_off_t pos;                /* Offset of next read in source file. */
  char *buf;                    /* Buffer for vdelta data. */
  apr_size_t saved_source_len;  /* Amount of source data saved in buf. */

  apr_md5_ctx_t context;        /* APR's MD5 context container. */

  /* Calculated digest from MD5 operations.
     NOTE:  This is only valid after this stream has returned the NULL
     (final) window.  */
  unsigned char digest[MD5_DIGESTSIZE]; 
};


/* Text delta applicator.  */

struct apply_baton {
  /* These are copied from parameters passed to svn_txdelta_apply.  */
  svn_stream_t *source;
  svn_stream_t *target;

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


/* Context/baton for building an operation sequence. */

struct build_ops_baton_t {
  int num_ops;                  /* current number of ops */
  int ops_size;                 /* number of ops allocated */
  svn_txdelta_op_t *ops;        /* the operations */

  svn_stringbuf_t *new_data;    /* any new data used by the operations */
};



/* Allocate a delta window. */

static svn_txdelta_window_t *
make_window (apr_pool_t *pool, struct build_ops_baton_t *bob)
{
  svn_txdelta_window_t *window;
  svn_string_t *new_data = apr_palloc (pool, sizeof (*new_data));

  window = apr_palloc (pool, sizeof (*window));
  window->sview_offset = 0;
  window->sview_len = 0;
  window->tview_len = 0;

  window->num_ops = bob->num_ops;
  window->ops = bob->ops;

  /* just copy the fields over, rather than alloc/copying into a whole new
     svn_string_t structure. */
  /* ### would be much nicer if window->new_data were not a ptr... */
  new_data->data = bob->new_data->data;
  new_data->len = bob->new_data->len;
  window->new_data = new_data;

  return window;
}



/* Insert a delta op into a delta window. */

void
svn_txdelta__insert_op (struct build_ops_baton_t *bob,
                        int opcode,
                        apr_off_t offset,
                        apr_off_t length,
                        const char *new_data,
                        apr_pool_t *pool)
{
  svn_txdelta_op_t *op;

  /* Create space for the new op. */
  if (bob->num_ops == bob->ops_size)
    {
      svn_txdelta_op_t *const old_ops = bob->ops;
      int const new_ops_size = (bob->ops_size == 0
                                ? 16 : 2 * bob->ops_size);
      bob->ops = apr_palloc (pool, new_ops_size * sizeof (*bob->ops));

      /* Copy any existing ops into the new array */
      if (old_ops)
        memcpy (bob->ops, old_ops,
                bob->ops_size * sizeof (*bob->ops));
      bob->ops_size = new_ops_size;
    }

  /* Insert the op. svn_delta_source and svn_delta_target are
     just inserted. For svn_delta_new, the new data must be
     copied into the window. */
  op = &bob->ops[bob->num_ops];
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
      op->offset = bob->new_data->len;
      op->length = length;
      svn_stringbuf_appendbytes (bob->new_data, new_data, length);
      break;
    default:
      assert (!"unknown delta op.");
    }

  ++bob->num_ops;
}



/* Allocate a delta stream descriptor. */

void
svn_txdelta (svn_txdelta_stream_t **stream,
             svn_stream_t *source,
             svn_stream_t *target,
             apr_pool_t *pool)
{
  *stream = apr_palloc (pool, sizeof (**stream));
  (*stream)->source = source; 
  (*stream)->target = target;
  (*stream)->more = TRUE;
  (*stream)->pos = 0;
  (*stream)->buf = apr_palloc (pool, 3 * SVN_STREAM_CHUNK_SIZE);
  (*stream)->saved_source_len = 0;

  /* Initialize MD5 digest calculation. */
  apr_md5_init (&((*stream)->context));
}



/* Pull the next delta window from a stream.

   Our current algorithm for picking source and target views is one
   step up from the dumbest algorithm of "compare corresponding blocks
   of each file."  A problem with that algorithm is that an insertion
   or deletion of N bytes near the beginning of the file will result
   in N bytes of non-overlap in each window from then on.  Our
   algorithm lessens this problem by "padding" the source view with
   half a target view's worth of data on each side.

   For example, suppose the target view size is 16K.  The dumbest
   algorithm would use bytes 0-16K for the first source view, 16-32K
   for the second source view, etc..  Our algorithm uses 0-24K for the
   first source view, 8-40K for the second source view, etc..
   Obviously, we're chewing some extra memory by doubling the source
   view size, but small (less than 8K) insertions or deletions won't
   result in non-overlap in every window.

   If we run out of source data before we run out of target data, we
   reuse the final chunk of data for the remaining windows.  No grand
   scheme at work there; that's just how the code worked out. */
svn_error_t *
svn_txdelta_next_window (svn_txdelta_window_t **window,
                         svn_txdelta_stream_t *stream,
                         apr_pool_t *pool)
{
  if (!stream->more)
    {
      apr_status_t apr_err;

      apr_err = apr_md5_final (stream->digest, &(stream->context));
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_create 
          (apr_err, 0, NULL, pool,
           "svn_txdelta_next_window: MD5 finalization failed");

      *window = NULL;
      return SVN_NO_ERROR;
    }
  else
    {
      svn_error_t *err;
      apr_size_t total_source_len;
      apr_size_t new_source_len = SVN_STREAM_CHUNK_SIZE;
      apr_size_t target_len = SVN_STREAM_CHUNK_SIZE;
      struct build_ops_baton_t bob = { 0 };

      /* If there is no saved source data yet, read an extra half
         window of data this time to get things started. */
      if (stream->saved_source_len == 0)
        new_source_len += SVN_STREAM_CHUNK_SIZE / 2;

      /* Read the source stream. */
      err = svn_stream_read (stream->source,
                             stream->buf + stream->saved_source_len,
                             &new_source_len);
      total_source_len = stream->saved_source_len + new_source_len;

      /* Update the MD5 accumulator with the freshly-read data in
         stream.

         ### todo: Currently, apr_md5_update() always returns
         APR_SUCCESS.  As such, we are proposing to the APR folks that
         its interface change to be a void function.  In the meantime,
         we'll simply ignore the return value. */
      apr_md5_update (&(stream->context), 
                      stream->buf + stream->saved_source_len,
                      new_source_len);

      /* Read the target stream. */
      if (err == SVN_NO_ERROR)
        err = svn_stream_read (stream->target, stream->buf + total_source_len,
                               &target_len);
      if (err != SVN_NO_ERROR)
        return err;
      stream->pos += new_source_len;

      /* Forget everything if there's no target data. */
      if (target_len == 0)
        {
          *window = NULL;
          stream->more = FALSE;
          return SVN_NO_ERROR;
        }

      /* Compute the delta operations. */
      bob.new_data = svn_stringbuf_create ("", pool);
      svn_txdelta__vdelta (&bob, stream->buf,
                           total_source_len, target_len,
                           pool);

      /* Create the delta window. */
      *window = make_window (pool, &bob);
      (*window)->sview_offset = stream->pos - total_source_len;
      (*window)->sview_len = total_source_len;
      (*window)->tview_len = target_len;

      /* Save the last window's worth of data from the source view. */
      stream->saved_source_len = (total_source_len < SVN_STREAM_CHUNK_SIZE)
        ? total_source_len : SVN_STREAM_CHUNK_SIZE;
      memmove (stream->buf,
               stream->buf + total_source_len - stream->saved_source_len,
               stream->saved_source_len);

      /* That's it. */
      return SVN_NO_ERROR;
    }
}


const unsigned char *
svn_txdelta_md5_digest (svn_txdelta_stream_t *stream)
{
  /* If there are more windows for this stream, the digest has not yet
     been calculated.  */
  if (stream->more)
    return NULL;

  return stream->digest;
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
  const svn_txdelta_op_t *op;
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
          assert (op->offset + op->length <= window->new_data->len);
          memcpy (tbuf + tpos,
                  window->new_data->data + op->offset,
                  op->length);
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
      svn_stream_close (ab->target);
      svn_pool_destroy (ab->pool);
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
          apr_size_t start = (apr_size_t)(window->sview_offset - ab->sbuf_offset);
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
      err = svn_stream_read (ab->source, ab->sbuf + ab->sbuf_len, &len);
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
  err = svn_stream_write (ab->target, ab->tbuf, &len);
  return err;
}


void
svn_txdelta_apply (svn_stream_t *source,
                   svn_stream_t *target,
                   apr_pool_t *pool,
                   svn_txdelta_window_handler_t *handler,
                   void **handler_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct apply_baton *ab;
  assert (pool != NULL);

  ab = apr_palloc (subpool, sizeof (*ab));
  ab->source = source;
  ab->target = target;
  ab->pool = subpool;
  ab->sbuf = NULL;
  ab->sbuf_size = 0;
  ab->sbuf_offset = 0;
  ab->sbuf_len = 0;
  ab->tbuf = NULL;
  ab->tbuf_size = 0;
  *handler = apply_window;
  *handler_baton = ab;
}



/* Convenience routines */

svn_error_t * 
svn_txdelta_send_string (const svn_string_t *string,
                         svn_txdelta_window_handler_t handler,
                         void *handler_baton,
                         apr_pool_t *pool)
{
  svn_txdelta_window_t window = { 0 };
  svn_txdelta_op_t op;

  /* Build a single `new' op */
  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = string->len;

  /* Build a single window containing a ptr to the string. */
  window.tview_len = string->len;
  window.num_ops = 1;
  window.ops = &op;
  window.new_data = string;

  /* Push the one window at the handler. */
  SVN_ERR ((*handler) (&window, handler_baton));
  
  /* Push a NULL at the handler, because we're done. */
  SVN_ERR ((*handler) (NULL, handler_baton));
  
  return SVN_NO_ERROR;
}

svn_error_t *svn_txdelta_send_stream (svn_stream_t *stream,
                                      svn_txdelta_window_handler_t handler,
                                      void *handler_baton,
                                      apr_pool_t *pool)
{
  svn_txdelta_stream_t *txstream;

  /* ### this is a hack. we should simply read from the stream, construct
     ### some windows, and pass those to the handler. there isn't any reason
     ### to crank up a full "diff" algorithm just to copy a stream.
     ###
     ### will fix RSN. */

  /* Create a delta stream which converts an *empty* bytestream into the
     target bytestream. */
  svn_txdelta (&txstream, svn_stream_empty (pool), stream, pool);
  return svn_txdelta_send_txstream (txstream, handler, handler_baton, pool);
}

svn_error_t *svn_txdelta_send_txstream (svn_txdelta_stream_t *txstream,
                                        svn_txdelta_window_handler_t handler,
                                        void *handler_baton,
                                        apr_pool_t *pool)
{
  svn_txdelta_window_t *window;

  /* create a pool just for the windows */
  apr_pool_t *wpool = svn_pool_create (pool);

  do
    {
      /* read in a single delta window */
      SVN_ERR( svn_txdelta_next_window (&window, txstream, wpool));

      /* shove it at the handler */
      SVN_ERR( (*handler)(window, handler_baton));

      /* free the window (if any) */
      svn_pool_clear (wpool);
    }
  while (window != NULL);

  svn_pool_destroy (wpool);

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
