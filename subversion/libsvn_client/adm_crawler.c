/*
 * adm_crawler.c:  report local WC mods to an xml parser.
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


#include "svn_types.h"
#include "svn_delta.h"
#include "apr_pools.h"
#include "apr_file_io.h"


/* Send the entire contents of XML_BUFFER to be parsed by XML_PARSER;
   then clear the buffer and return. */
static svn_error_t *
flush_xml_buffer (svn_string_t *xml_buffer,
                  svn_xml_parser_t *xml_parser)
{
  svn_error_t *err = svn_xml_parsebytes (xml_buffer->data,
                                         xml_buffer->len,
                                         0,
                                         xml_parser);
  if (err)
    return err;

  svn_string_setempty (xml_buffer);

  return SVN_NO_ERROR;
}


/* Fetch the next child subdirectory of CURRENT_DIR by searching in
   DIRHANDLE.  If there are no more subdir children, return NULL.  */
static svn_string_t *
get_next_child_subdir (svn_string_t *current_dir,
                       apr_dir_t *dirhandle,
                       apr_pool_t *pool)
{
  /* Todo: get this right, using apr's functions for grabbing "next"
     dirent in a apr_dir_t and examining the "current dirent".  */
}


/* Return a bytestring containing the contents of `DIR/SVN/delta_here',
   using whatever abstraction methods libsvn_wc implements.  If the
   file is empty, return NULL instead.  */
static svn_string_t *
get_delta_here_contents (svn_string_t *dir,
                         apr_pool_t *pool)
{
  svn_string_t *localmod_buffer = svn_string_create ("", pool);

  /* Have libsvn_wc return a filehandle to `delta_here' using its
     abstract methods to search DIR */
  apr_file_t *delta_here = svn_wc_get_delta_here (dir);

  apr_open (delta_here);
  apr_full_read (delta_here, localmod_buffer);
  apr_close (delta_here);

  if (file is empty)
    return NULL;

  else
    return localmod_buffer;
}




/* Recursive working-copy crawler. Push xml out to parser when
   appropriate. */
static svn_error_t *
do_crawl (svn_string_t *current_dir,
          svn_string_t *xml_buffer,
          svn_xml_parser_t *xml_parser,
          apr_pool_t *pool)

{
  svn_error_t *err;
  svn_string_t *child;
  int fruitful = 0;

  /* grab contents of current `delta-here' file */
  svn_string_t *localmod_buffer = get_delta_here_contents (current_dir);
  
  /* if non-empty, send the contents to the parser */
  if (localmod_buffer)
    {
      svn_string_appendbytes (xml_buffer, localmod_buffer, pool)
      err = flush_xml_buffer (xml_buffer, xml_parser);
      if (err)
        return err;
      fruitful = 1;
    }

  /* recurse depth-first */

  while (child = get_next_child_subdir ())
    {
      write 3 "down" tags into xml_buffer;
      err = do_crawl (child);

      if (err->apr_err == SVN_NO_ERROR)
        fruitful = 1;
      
      else if (err->apr_err == SVN_ERR_UNFRUITFUL_DESCENT)
        /* effectively "undo" the descent tags */
        remove 3 "down" tags from xml_buffer;

      else /* uh-oh, a _real_ error */
        return err;
    } 
  
  if (fruitful) 
    {
      write 3 "up" tags into xml_buffer;
      return SVN_NO_ERROR;
    }

  else
    return SVN_ERR_UNFRUITFUL_DESCENT;
}



/* Public interface.

   Do a depth-first crawl of the local changes in a working copy,
   beginning at ROOT_DIRECTORY.  Push synthesized xml (representing a
   coherent tree-delta) at XML_PARSER.

   Presumably, the client library will grab a "walker" from libsvn_ra,
   build an svn_xml_parser_t around it, and then pass the parser to
   this routine.  This is how local changes in the working copy are
   ultimately translated into network requests.  */

svn_error_t *
svn_cl_crawl_local_mods (svn_string_t *root_directory,
                         svn_xml_parser_t *xml_parser,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  
  svn_string_t *xml_buffer = svn_string_create ("", pool);

  /* Always begin with a lone "<text-delta"> */
  svn_string_appendbytes (xml_buffer, "<text-delta>", 12, pool);

  /* Do the recursive crawl, starting at the root directory.  */
  err = do_crawl (root_directory, xml_buffer, xml_parser, pool);

  if (err->apr_err == SVN_NO_ERROR)
    {
      /* The descent was fruitful. */

      /* Always finish with a lone "</text-delta>" */
      svn_string_appendbytes (xml_buffer, "</text-delta>", 13, pool);
      
      /* Send whatever xml is left in the buffer. */
      err = flush_xml_buffer (xml_buffer, xml_parser);
      if (err)
        return err;

      return SVN_NO_ERROR;
    }

  if (err->apr_err == SVN_ERR_UNFRUITFUL_DESCENT)
    /* There were *no* local mods *anywhere* in the tree!  That's
       okay.  The parser gets no xml data from us.  Just return. */
    return SVN_NO_ERROR;

  else /* uh-oh, a _real_ error was passed back. */
    return err;
}








/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
