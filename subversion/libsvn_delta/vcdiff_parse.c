/*
 * vcdiff_parse.c:  routines to parse VCDIFF data
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

/* ==================================================================== */



/* This file is a dummy implementation of the vcdiff interface.

   NOTE that (at least in this model), our vcdiff parser creates a new
   APR sub-pool to buffer each incoming window of data.  It then
   passes this window off to the consumer routine, and creates a *new*
   sub-pool to start buffering again.  It's the consumer routine's
   responsibility to free the sub-pool (when it's finished with it) by
   calling svn_free_delta_window().

*/



#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_pools.h"
#include "delta.h"


/* How many bytes should each vcdiff window be? */

#define SVN_VCDIFF_WINDOW_SIZE 5



/* Return a vcdiff parser object, PARSER.  If we're receiving a
   vcdiff-format byte stream, one block of bytes at a time, we can
   pass each block in succession to svn_delta__vcdiff_parse, with
   PARSER as the other argument.  PARSER keeps track of where we are
   in the stream; each time we've received enough data for a complete
   svn_delta_window_t, we pass it to HANDLER, along with
   HANDLER_BATON.  POOL will be used to by PARSER to buffer the
   incoming vcdiff data and create windows to send off.  */
svn_vcdiff_parser_t *
svn_make_vcdiff_parser (svn_txdelta_window_handler_t *handler,
                        void *handler_baton,
                        apr_pool_t *pool)
{
  /* Allocate a vcdiff_parser and fill out its fields */
  svn_vcdiff_parser_t *new_vcdiff_parser = 
    (svn_vcdiff_parser_t *)
    apr_palloc (pool, sizeof(svn_vcdiff_parser_t));

  new_vcdiff_parser->consumer_func = handler;
  new_vcdiff_parser->consumer_baton = handler_baton;

  new_vcdiff_parser->pool = pool;
  new_vcdiff_parser->subpool = 
    apr_make_sub_pool (new_vcdiff_parser->pool, NULL);

  /* Important:  notice that the parser's buffer lives in a subpool */
  new_vcdiff_parser->buffer = 
    svn_string_create ("", new_vcdiff_parser->subpool);
  
  return new_vcdiff_parser;
}




/* Create a new window from PARSER->SUBPOOL, and send it off to the
   caller's consumer routine, then create a new SUBPOOL in PARSER so
   that it can continue buffering data.  */

/* Note: this dummy routine assumes there's nothing but raw ASCII in
   the buffer, and only creates a single kind of txdelta_op: the
   "append new text" kind.  It places one such op into a window and
   sends the window off.  The *real* vcdiff code will place a number
   of ops into a window, based on the decoded bytestream.  */
svn_error_t *
svn_vcdiff_send_window (svn_vcdiff_parser_t *parser, apr_size_t len)
{
  svn_error_t *err;

  svn_txdelta_window_t *window =
    (svn_txdelta_window_t *) apr_palloc (parser->subpool, 
                                       sizeof(svn_txdelta_window_t));
  
  svn_txdelta_op_t *new_op = 
    (svn_txdelta_op_t *) 
    apr_palloc (parser->subpool, sizeof(svn_txdelta_op_t));
  
  /* Right now, we have only one kind of vcdiff operation:
     "create new text" :) */
  new_op->action_code = svn_txdelta_new;  /* append new text */
  new_op->offset = 0;
  new_op->length = len;
  
  window->pool = parser->subpool;
  window->num_ops = 1;
  window->ops = new_op;
  window->new = parser->buffer;  /* just give away our whole
                                    parser-buffer to the
                                    consumer... it will free the whole
                                    subpool later on. */
  
  /* Pass this new window to the caller's consumer routine, if any */
  if (parser->consumer_func)
    {
      err = (* (parser->consumer_func)) (window, parser->consumer_baton);
      if (err)
        return svn_quick_wrap_error 
          (err, "svn_vcdiff_send_window: consumer_func choked.");
    }
  
  /* Now that the window consumer is done, free the window/sub-pool */

  apr_destroy_pool (window->pool);

  /* Make a new subpool to continue buffering. */

  parser->subpool = apr_make_sub_pool (parser->pool, NULL);

  parser->buffer = svn_string_create ("", parser->subpool);
  
  return SVN_NO_ERROR;
}




/* Parse another block of bytes in the vcdiff-format stream managed by
   PARSER.  When we've accumulated enough data for a complete window,
   call svn_vcdiff_send_window().  */

/* Note: this dummy routine thinks a "window" is just a certain number
   of bytes received.  The *real* vcdiff code will decode the vcdiff
   bytestream into a number of semantic svn_txdelta_ops, place these
   ops into a window, and send it off.  This routine only works right
   now so long as our text-deltas are just plain, uncoded ASCII.  */
svn_error_t *
svn_vcdiff_parse (svn_vcdiff_parser_t *parser,
                  const char *buffer,
                  apr_size_t len)
{
  svn_error_t *err;
  apr_size_t i = 0;  /* This is our offset into BUFFER */

  if (len == 0)  
    {
      /* This means the caller has finished sending the vcdiff
         bytestream, so flush any remaining bytes in our buffer.  */
      err = svn_vcdiff_send_window (parser, parser->buffer->len);
      if (err)
        return err;

      return SVN_NO_ERROR;
    }

  /* else... */

  do    /* Loop over all bytes received in BUFFER */
    {
      /* Do we have enough bytes in our parser's buffer to send off a
         new window?  */
      if (parser->buffer->len == SVN_VCDIFF_WINDOW_SIZE)
        {
          /* Send off exactly that many bytes as a window to the
             consumer routine */
          err = svn_vcdiff_send_window (parser, SVN_VCDIFF_WINDOW_SIZE);
          if (err)
            return err;
        }

      else  /* don't yet have enough bytes for a window */
        {
          /* kff todo: in real life, eat more than a byte at a time */
          /* So just copy the next byte in BUFFER to PARSER->BUFFER */
          svn_string_appendbytes (parser->buffer, 
                                  (buffer + i), 1,
                                  parser->subpool);
          i++;
        }
      
    } while (i < len);


    return SVN_NO_ERROR;
}






