/*
 * prop_parse.c:  routines to parse property-delta data
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

/* 

  The property-parsing system is modeled closely after the
  vcdiff-parser API, except that it has two alternate modes of
  operation.  If the caller sets apply_*_propchange in the walker,
  then we buffer and send off the entire* propchange in RAM.  If the
  caller gives us an svn_prop_change_chunk_handler_t, however, then it
  wants the propchange streamed in a chunky way, just like text
  deltas.  These two methods are *not* mutually exclusive.  

   NOTE that (at least in this model), our parser creates a new APR
   sub-pool to buffer each incoming window of data.  It then passes
   this window off to the consumer routine, and creates a *new*
   sub-pool to start buffering again.  It's the consumer routine's
   responsibility to free the sub-pool (when it's finished with it) by
   calling svn_free_delta_chunk().

*/


#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_pools.h"
#include "delta.h"




/* Utility: deallocate a parser's subpool if it exists (and thereby
   the propchange inside it), and then create a new subpool with a new
   propchange, ready to buffer the next change.  */
void 
svn_delta__reset_parser_subpool (svn_delta__pdelta_parser_t *parser)
{
  if (parser->subpool)
    apr_destroy_pool (parser->subpool);

  parser->subpool = apr_make_sub_pool (parser->pool, NULL);

  parser->propchange = 
    (svn_propchange_t *) apr_palloc (parser->subpool, 
                                     sizeof(svn_propchange_t));
 
  parser->propchange->value = 
    svn_string_create ("", parser->subpool);
}



/* Return a prop-chunkparser object, PARSER.  If we're receiving a
   propchange byte stream, one block of bytes at a time, we can pass
   each block in succession to svn_propchunk_parse, with PARSER as the
   other argument.  PARSER keeps track of where we are in the stream;
   each time we've received enough data for a complete chunk, we pass
   it to HANDLER, along with HANDLER_BATON.  POOL will be used to by
   PARSER to buffer the incoming data and create chunk to send off.  */
svn_delta__pdelta_parser_t *
svn_delta__make_pdelta_parser (svn_propchange_handler_t *handler,
                               void *handler_baton,
                               apr_pool_t *pool)
{
  /* Allocate a vcdiff_parser and fill out its fields */
  svn_delta__pdelta_parser_t *new_pdelta_parser = 
    (svn_delta__pdelta_parser_t *) 
    apr_palloc (pool, sizeof(svn_delta__pdelta_parser_t));

  new_pdelta_parser->handler = handler;
  new_pdelta_parser->baton = handler_baton;

  new_pdelta_parser->pool = pool;

  /* Create a subpool containing an empty propchange object. */
  svn_delta__reset_parser_subpool (new_pdelta_parser);

  return new_pdelta_parser;
}





/* Buffer up incoming data within a <set> tag. */
svn_error_t *
svn_delta__pdelta_parse (svn_delta__digger_t *digger,
                         const char *buffer,
                         apr_off_t *len)
{
  svn_delta__pdelta_parser_t *parser = digger->pdelta_parser;

  svn_string_appendbytes (parser->propchange->value,
                          buffer, *len,
                          parser->subpool);

  return SVN_NO_ERROR;
}



