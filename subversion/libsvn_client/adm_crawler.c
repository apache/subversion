/*
 * adm_crawler.c:  depth-first search `SVN/delta-here' & output XML stream
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


#include <svn_types.h>         /* defines svn_read_fn_t */



typedef struct svn_client__crawl_baton
{
  svn_string_t *xml_buffer;
  apr_off_t *last_byte_sent;

  svn_string_t *current_directory;

  stack;

} svn_client__crawl_baton_t;




/* The public interface, a `svn_delta_read_fn_t' routine.

   (This routine reads LEN bytes of data from BATON and stores them in
   BUFFER; POOL is used for any allocations needed.)

   In particular, this routine does a depth-first "crawl" of the local
   changes in a working copy, and outputs a coherent XML stream (in
   our tree-delta format).

   Eventually libsvn_client will grab this routine along with an
   svn_delta_walk_t structure from libsvn_ra, and "plug them together"
   by feeding them into svn_xml_parse().  As a result, local changes
   in the working copy are translated into network requests.  */

svn_error_t *
svn_cl_crawl_adm_dirs (void *baton,
                           char *buffer,
                           apr_off_t *len,
                           apr_pool_t *pool)
{
  svn_client__crawl_baton_t crawl_baton = (svn_client__crawl_baton_t *) baton;

  if (crawl_baton->xml_buffer->len >= len)
    {
      /* memcpy LEN bytes from xml_buffer into BUFFER, specifically starting at  */
      
    }

}



/* Depth-first search beginning at DIRECTORY.  Pass NULL for value of
   STACK; STACK will be built-up internally during recursion.  */

static void
search_tree (directory, stack)
{
  if (stack == NULL)
    {
      /* We're at the beginning of our recursion */
    }



}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
