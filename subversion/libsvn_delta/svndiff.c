/* 
 * svndiff.c -- Encoding and decoding svndiff-format deltas.
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
#include "svn_delta.h"
#include "svn_io.h"
#include "delta.h"
#include "svn_pools.h"

#define NORMAL_BITS 7
#define LENGTH_BITS 5


/* ----- Text delta to svndiff ----- */

/* We make one of these and get it passed back to us in calls to the
   window handler.  We only use it to record the write function and
   baton passed to svn_txdelta_to_svndiff ().  */
struct encoder_baton {
  svn_stream_t *output;
  svn_boolean_t header_done;
  apr_pool_t *pool;
};


/* Encode VAL into the buffer P using the variable-length svndiff
   integer format.  Return the incremented value of P after the
   encoded bytes have been written.

   This encoding uses the high bit of each byte as a continuation bit
   and the other seven bits as data bits.  High-order data bits are
   encoded first, followed by lower-order bits, so the value can be
   reconstructed by concatenating the data bits from left to right and
   interpreting the result as a binary number.  Examples (brackets
   denote byte boundaries, spaces are for clarity only):

           1 encodes as [0 0000001]
          33 encodes as [0 0100001]
         129 encodes as [1 0000001] [0 0000001]
        2000 encodes as [1 0001111] [0 1010000]
*/

static char *
encode_int (char *p, apr_off_t val)
{
  int n;
  apr_off_t v;
  unsigned char cont;

  assert (val >= 0);

  /* Figure out how many bytes we'll need.  */
  v = val >> 7;
  n = 1;
  while (v > 0)
    {
      v = v >> 7;
      n++;
    }

  /* Encode the remaining bytes; n is always the number of bytes
     coming after the one we're encoding.  */
  while (--n >= 0)
    {
      cont = ((n > 0) ? 0x1 : 0x0) << 7;
      *p++ = (char)(((val >> (n * 7)) & 0x7f) | cont);
    }

  return p;
}


/* Append an encoded integer to a string.  */
static void
append_encoded_int (svn_stringbuf_t *header, apr_off_t val, apr_pool_t *pool)
{
  char buf[128], *p;

  p = encode_int (buf, val);
  svn_stringbuf_appendbytes (header, buf, p - buf);
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  struct encoder_baton *eb = baton;
  apr_pool_t *pool = svn_pool_create (eb->pool);
  svn_stringbuf_t *instructions = svn_stringbuf_create ("", pool);
  svn_stringbuf_t *header = svn_stringbuf_create ("", pool);
  char ibuf[128], *ip;
  const svn_txdelta_op_t *op;
  svn_error_t *err;
  apr_size_t len;

  /* Make sure we write the header.  */
  if (eb->header_done == FALSE)
    {
      len = 4;
      err = svn_stream_write (eb->output, "SVN\0", &len);
      if (err != SVN_NO_ERROR)
        return err;
      eb->header_done = TRUE;
    }

  if (window == NULL)
    {
      svn_stream_t *output = eb->output;

      /* We're done; clean up.

         We clean our pool first. Given that the output stream was passed
         TO us, we'll assume it has a longer lifetime, and that it will not
         be affected by our pool destruction.

         The contrary point of view (close the stream first): that could
         tell our user that everything related to the output stream is done,
         and a cleanup of the user pool should occur. However, that user
         pool could include the subpool we created for our work (eb->pool),
         which would then make our call to svn_pool_destroy() puke.
       */
      svn_pool_destroy (eb->pool);

      return svn_stream_close (output);
    }

  /* Encode the instructions.  */
  for (op = window->ops; op < window->ops + window->num_ops; op++)
    {
      /* Encode the action code and length.  */
      ip = ibuf;
      switch (op->action_code)
        {
        case svn_txdelta_source: *ip = (char)0; break;
        case svn_txdelta_target: *ip = (char)(0x1 << 6); break;
        case svn_txdelta_new:    *ip = (char)(0x2 << 6); break;
        }
      if (op->length >> 6 == 0)
        *ip++ |= op->length;
      else
        ip = encode_int (ip + 1, op->length);
      if (op->action_code != svn_txdelta_new)
        ip = encode_int (ip, op->offset);
      svn_stringbuf_appendbytes (instructions, ibuf, ip - ibuf);
    }

  /* Encode the header.  */
  append_encoded_int (header, window->sview_offset, pool);
  append_encoded_int (header, window->sview_len, pool);
  append_encoded_int (header, window->tview_len, pool);
  append_encoded_int (header, instructions->len, pool);
  append_encoded_int (header, window->new_data->len, pool);

  /* Write out the window.  */
  len = header->len;
  err = svn_stream_write (eb->output, header->data, &len);
  if (err == SVN_NO_ERROR && instructions->len > 0)
    {
      len = instructions->len;
      err = svn_stream_write (eb->output, instructions->data, &len);
    }
  if (err == SVN_NO_ERROR && window->new_data->len > 0)
    {
      len = window->new_data->len;
      err = svn_stream_write (eb->output, window->new_data->data, &len);
    }

  svn_pool_destroy (pool);
  return err;
}

void
svn_txdelta_to_svndiff (svn_stream_t *output,
			apr_pool_t *pool,
			svn_txdelta_window_handler_t *handler,
			void **handler_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct encoder_baton *eb;

  eb = apr_palloc (subpool, sizeof (*eb));
  eb->output = output;
  eb->header_done = FALSE;
  eb->pool = subpool;

  *handler = window_handler;
  *handler_baton = eb;
}



/* ----- svndiff to text delta ----- */

/* An svndiff parser object.  */
struct decode_baton
{
  /* Once the svndiff parser has enough data buffered to create a
     "window", it passes this window to the caller's consumer routine.  */
  svn_txdelta_window_handler_t consumer_func;
  void *consumer_baton;

  /* Pool to create subpools from; each developing window will be a
     subpool.  */
  apr_pool_t *pool;

  /* The current subpool which contains our current window-buffer.  */
  apr_pool_t *subpool;

  /* The actual svndiff data buffer, living within subpool.  */
  svn_stringbuf_t *buffer;

  /* The offset and size of the last source view, so that we can check
     to make sure the next one isn't sliding backwards.  */
  apr_off_t last_sview_offset;
  apr_size_t last_sview_len;

  /* We have to discard four bytes at the beginning for the header.
     This field keeps track of how many of those bytes we have read.  */
  int header_bytes;

  /* Do we want an error to occur when we close the stream that
     indicates we didn't send the whole svndiff data?  If you plan to
     not transmit the whole svndiff data stream, you will want this to
     be FALSE. */
  svn_boolean_t error_on_early_close;
};


/* Decode an svndiff-encoded integer into VAL and return a pointer to
   the byte after the integer.  The bytes to be decoded live in the
   range [P..END-1].  See the comment for encode_int earlier in this
   file for more detail on the encoding format.  */

static const unsigned char *
decode_int (apr_off_t *val,
            const unsigned char *p,
            const unsigned char *end)
{
  /* Decode bytes until we're done.  */
  *val = 0;
  while (p < end)
    {
      *val = (*val << 7) | (*p & 0x7f);
      if (((*p++ >> 7) & 0x1) == 0)
        return p;
    }
  return NULL;
}


/* Decode an instruction into OP, returning a pointer to the text
   after the instruction.  Note that if the action code is
   svn_txdelta_new, the opcode field of *OP will not be set.  */

static const unsigned char *
decode_instruction (svn_txdelta_op_t *op,
                    const unsigned char *p,
                    const unsigned char *end)
{
  apr_off_t val;

  if (p == end)
    return NULL;

  /* Decode the instruction selector.  */
  switch ((*p >> 6) & 0x3)
    {
    case 0x0: op->action_code = svn_txdelta_source; break;
    case 0x1: op->action_code = svn_txdelta_target; break;
    case 0x2: op->action_code = svn_txdelta_new; break;
    case 0x3: return NULL;
    }

  /* Decode the length and offset.  */
  op->length = *p++ & 0x3f;
  if (op->length == 0)
    {
      p = decode_int (&val, p, end);
      if (p == NULL)
        return NULL;
      op->length = val;
    }
  if (op->action_code != svn_txdelta_new)
    {
      p = decode_int (&val, p, end);
      if (p == NULL)
        return NULL;
      op->offset = val;
    }

  return p;
}

/* Count the instructions in the range [P..END-1] and make sure they
   are valid for the given window lengths.  Return -1 if the
   instructions are invalid; otherwise return the number of
   instructions.  */
static svn_error_t *
count_and_verify_instructions (int *ninst,
                               const unsigned char *p,
                               const unsigned char *end,
                               apr_size_t sview_len,
                               apr_size_t tview_len,
                               apr_size_t new_len,
                               apr_pool_t *pool)
{
  int n = 0;
  svn_txdelta_op_t op = { 0 };
  apr_size_t tpos = 0, npos = 0;

  while (p < end)
    {
      p = decode_instruction (&op, p, end);
      if (p == NULL || op.offset < 0 || op.length <= 0
          || op.length > tview_len - tpos)
        {
          if (p == NULL)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "insn %d cannot be decoded", n);
          else if (op.offset < 0)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "insn %d has negative offset", n);
          else if (op.length <= 0)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "insn %d has non-positive length", n);
          else
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "insn %d overflows the target view", n);
        }

      switch (op.action_code)
        {
        case svn_txdelta_source:
          if (op.length > sview_len - op.offset)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "[src] insn %d overflows the source view", n);
          break;
        case svn_txdelta_target:
          if (op.offset >= tpos)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "[tgt] insn %d starts beyond the target view position", n);
          break;
        case svn_txdelta_new:
          if (op.length > new_len - npos)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
               "[new] insn %d overflows the new data section", n);
          npos += op.length;
          break;
        }
      tpos += op.length;
      n++;
    }
  if (tpos != tview_len)
    return svn_error_create (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
                             "delta does not fill the target window");
  if (npos != new_len)
    return svn_error_create (SVN_ERR_SVNDIFF_INVALID_OPS, 0, NULL, pool,
                             "delta does not contain enough new data");

  *ninst = n;
  return SVN_NO_ERROR;
}

static svn_error_t *
write_handler (void *baton,
               const char *buffer,
               apr_size_t *len)
{
  struct decode_baton *db = (struct decode_baton *) baton;
  const unsigned char *p, *end;
  apr_off_t val, sview_offset;
  apr_size_t sview_len, tview_len, inslen, newlen, remaining, npos;
  svn_txdelta_op_t *op;
  int ninst;

  /* Chew up four bytes at the beginning for the header.  */
  if (db->header_bytes < 4)
    {
      apr_size_t nheader = 4 - db->header_bytes;
      if (nheader > *len)
        nheader = *len;
      if (memcmp (buffer, "SVN\0" + db->header_bytes, nheader) != 0)
        return svn_error_create (SVN_ERR_SVNDIFF_INVALID_HEADER, 
                                 0, NULL, db->pool,
                                 "svndiff has invalid header");
      *len -= nheader;
      buffer += nheader;
      db->header_bytes += nheader;
    }

  /* Concatenate the old with the new.  */
  svn_stringbuf_appendbytes (db->buffer, buffer, *len);

  /* We have a buffer of svndiff data that might be good for:

     a) an integral number of windows' worth of data - this is a
        trivial case.  Make windows from our data and ship them off.

     b) a non-integral number of windows' worth of data - we shall
        consume the integral portion of the window data, and then
        somewhere in the following loop the decoding of the svndiff
        data will run out of stuff to decode, and will simply return
        SVN_NO_ERROR, anxiously awaiting more data.
  */

  while (1)
    {
      apr_pool_t *newpool;
      svn_txdelta_window_t window = { 0 };
      svn_string_t new_data;
      svn_txdelta_op_t *ops;

      /* Read the header, if we have enough bytes for that.  */
      p = (const unsigned char *) db->buffer->data;
      end = (const unsigned char *) db->buffer->data + db->buffer->len;

      p = decode_int (&val, p, end);
      if (p == NULL)
	return SVN_NO_ERROR;
      sview_offset = val;

      p = decode_int (&val, p, end);
      if (p == NULL)
	return SVN_NO_ERROR;
      sview_len = val;

      p = decode_int (&val, p, end);
      if (p == NULL)
	return SVN_NO_ERROR;
      tview_len = val;

      p = decode_int (&val, p, end);
      if (p == NULL)
	return SVN_NO_ERROR;
      inslen = val;

      p = decode_int (&val, p, end);
      if (p == NULL)
	return SVN_NO_ERROR;
      newlen = val;

      /* Check for integer overflow (don't want to let the input trick
         us into invalid pointer games using negative numbers).  */
      /* FIXME: Some of these are apr_size_t, which is
         unsigned. Should they be apr_ptrdiff_t instead? --xbc */
      if (sview_offset < 0 || sview_len < 0 || tview_len < 0 || inslen < 0
	  || newlen < 0 || inslen + newlen < 0 || sview_offset + sview_len < 0)
	return svn_error_create (SVN_ERR_SVNDIFF_CORRUPT_WINDOW, 0, NULL, 
				 db->pool,
				 "svndiff contains corrupt window header");

      /* Check for source windows which slide backwards.  */
      if (sview_len > 0
          && (sview_offset < db->last_sview_offset
              || (sview_offset + sview_len
                  < db->last_sview_offset + db->last_sview_len)))
	return svn_error_create (SVN_ERR_SVNDIFF_BACKWARD_VIEW, 0, NULL, 
				 db->pool,
				 "svndiff has backwards-sliding source views");

      /* Wait for more data if we don't have enough bytes for the
         whole window.  */
      if ((apr_size_t) (end - p) < inslen + newlen)
	return SVN_NO_ERROR;

      /* Count the instructions and make sure they are all valid.  */
      end = p + inslen;
      SVN_ERR (count_and_verify_instructions (&ninst, p, end, sview_len, 
                                              tview_len, newlen, db->pool));

      /* Build the window structure.  */
      window.sview_offset = sview_offset;
      window.sview_len = sview_len;
      window.tview_len = tview_len;

      ops = apr_palloc (db->subpool, ninst * sizeof (*ops));
      npos = 0;
      for (op = ops; op < ops + ninst; op++)
	{
          /* FIXME: The way things stand now, every svndiff insn is decoded
             twice. We should integrate what count_and_verify_instructions
             does here, instead.  --xbc */
	  p = decode_instruction (op, p, end);
	  if (op->action_code == svn_txdelta_new)
	    {
	      op->offset = npos;
	      npos += op->length;
	    }
	}
      window.num_ops = ninst;
      window.ops = ops;

      new_data.data = (const char *)p;
      new_data.len = newlen;
      window.new_data = &new_data;

      /* Send it off.  */
      SVN_ERR(db->consumer_func (&window, db->consumer_baton));

      /* Make a new subpool and buffer, saving aside the remaining
         data in the old buffer.  */
      newpool = svn_pool_create (db->pool);
      p += newlen;
      remaining = db->buffer->data + db->buffer->len - (const char *) p;
      db->buffer = 
	svn_stringbuf_ncreate ((const char *) p, remaining, newpool);

      /* Remember the offset and length of the source view for next time.  */
      db->last_sview_offset = sview_offset;
      db->last_sview_len = sview_len;

      /* We've copied stuff out of the old pool. Toss that pool and use
         our new pool.
         ### might be nice to avoid the copy and just use svn_pool_clear
         ### to get rid of whatever the "other stuff" is. future project...
      */
      svn_pool_destroy(db->subpool);
      db->subpool = newpool;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_handler (void *baton)
{
  struct decode_baton *db = (struct decode_baton *) baton;
  svn_error_t *err;

  /* Make sure that we're at a plausible end of stream, returning an
     error if we are expected to do so.  */
  if ((db->error_on_early_close)
      && (db->header_bytes < 4 || db->buffer->len != 0))
    return svn_error_create (SVN_ERR_SVNDIFF_UNEXPECTED_END, 0, NULL, db->pool,
                             "unexpected end of svndiff input");

  /* Tell the window consumer that we're done, and clean up.  */
  err = db->consumer_func (NULL, db->consumer_baton);
  svn_pool_destroy (db->pool);
  return err;
}


svn_stream_t *
svn_txdelta_parse_svndiff (svn_txdelta_window_handler_t handler,
                           void *handler_baton,
                           svn_boolean_t error_on_early_close,
                           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct decode_baton *db = apr_palloc (pool, sizeof (*db));
  svn_stream_t *stream;

  db->consumer_func = handler;
  db->consumer_baton = handler_baton;
  db->pool = subpool;
  db->subpool = svn_pool_create (subpool);
  db->buffer = svn_stringbuf_create ("", db->subpool);
  db->last_sview_offset = 0;
  db->last_sview_len = 0;
  db->header_bytes = 0;
  db->error_on_early_close = error_on_early_close;
  stream = svn_stream_create (db, pool);
  svn_stream_set_write (stream, write_handler);
  svn_stream_set_close (stream, close_handler);
  return stream;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
