/*
 * adm_crawler.c:  report local WC mods to an Editor.
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


#include <apr_pools.h>
#include <apr_file_io.h>
#include "wc.h"
#include "svn_wc.h"
#include "svn_delta.h"




/* Send the entire contents of XML_BUFFER to be parsed by XML_PARSER;
   then clear the buffer and return. */
static svn_error_t *
flush_xml_buffer (svn_string_t *xml_buffer,
                  svn_delta_xml_parser_t *xml_parser)
{
  svn_error_t *err = svn_delta_xml_parsebytes (xml_buffer->data,
                                               xml_buffer->len,
                                               0,
                                               xml_parser);
  if (err)
    return err;

  svn_string_setempty (xml_buffer);

  return SVN_NO_ERROR;
}





/* Return the NAME of the next child subdir of DIRHANDLE.  DIRHANDLE
   is assumed to be already open.  If there are no more subdir
   children, return NULL.  */
static svn_error_t *
get_next_child_subdir (svn_string_t **name,
                       apr_dir_t *dirhandle,
                       apr_pool_t *pool)
{
  apr_status_t status;
  char *entryname;
  apr_filetype_e entrytype;

  do {
    /* Read the next entry from dirhandle, get its name and type, too. */
    status = apr_readdir (dirhandle);
    if (status == APR_ENOENT) /* no more entries */
      {
        *name = NULL;
        return SVN_NO_ERROR;
      }
    else if (status)
      return svn_create_error (status, 0, NULL, pool, "apr_readdir choked.");

    status = apr_get_dir_filename (&entryname, dirhandle);
    if (status)
      return svn_create_error (status, 0, NULL, pool,
                               "apr_get_dir_filename choked.");

    status = apr_dir_entry_ftype (&entrytype, dirhandle);
    if (status)
      return svn_create_error (status, 0, NULL, pool,
                               "apr_dir_entry_ftype choked.");

    /* Exit the loop if the entry is a subdir AND isn't "." or ".." */
    if ( (entrytype == APR_DIR)
         && (strcmp (entryname, "."))
         && (strcmp (entryname, "..")) )
      {
        *name = svn_string_create (entryname, pool);
        return SVN_NO_ERROR;
      }

  } while (1);
  
}





/* Return a bytestring STR containing the contents of
   `DIR/SVN/delta_here', using whatever abstraction methods libsvn_wc
   implements.  If the file is empty, set STR to NULL instead.  */
static svn_error_t *
get_delta_here_contents (svn_string_t **str,
                         svn_string_t *dir,
                         apr_pool_t *pool)
{
  char buf[BUFSIZ];
  svn_error_t *err;
  apr_size_t bytes_read;
  apr_status_t status = 0;
  apr_file_t *filehandle = NULL;
  svn_string_t *localmod_buffer = svn_string_create ("", pool);

  /* Have libsvn_wc return a filehandle to `delta_here' using its
     abstract methods to search DIR */
  err = svn_wc__open_adm_file (&filehandle, dir,
                               SVN_WC__ADM_DELTA_HERE,
                               APR_READ, pool);
  if (err)
    return err;

  /* Copy the contents of the file into a bytestring */
  while (status != APR_EOF)
    {
      status = apr_full_read (filehandle, buf, BUFSIZ, &bytes_read);
      if (status && (status != APR_EOF))
        return svn_create_error (status, 0, NULL, pool,
                                 "apr_full_read choked");
      svn_string_appendbytes (localmod_buffer, buf, bytes_read, pool);
    }
  err = svn_wc__close_adm_file (filehandle, dir,
                                SVN_WC__ADM_DELTA_HERE, 0, pool);
  if (err)
    return err;

  if (svn_string_isempty (localmod_buffer))
    *str = NULL;

  else
    *str = localmod_buffer;

  return SVN_NO_ERROR;
}





/* The recursive working-copy crawler. Push xml out to parser when
   appropriate. */
static svn_error_t *
do_crawl (svn_string_t *current_dir,
          svn_string_t *xml_buffer,
          svn_delta_xml_parser_t *xml_parser,
          apr_pool_t *pool)

{
  svn_error_t *err;
  apr_status_t status;
  apr_dir_t *thisdir;
  svn_string_t *child = NULL;
  int fruitful = 0;
  svn_string_t *localmod_buffer = NULL;

  /* Grab contents of current `delta-here' file, place into
     localmod_buffer. */
  err = get_delta_here_contents (&localmod_buffer, current_dir, pool);
  if (err)
    return err;
  
  /* If non-NULL, append to our xml_buffer and send everything to parser */
  if (localmod_buffer)
    {
      svn_string_appendstr (xml_buffer, localmod_buffer, pool);
      err = flush_xml_buffer (xml_buffer, xml_parser);
      if (err)
        return err;
      fruitful = 1;
    }

  /* Open the current directory */
  status = apr_opendir (&thisdir, current_dir->data, pool);
  if (status)
    return svn_create_error (status, 0, NULL, pool, "apr_opendir choked.");

  /* Get the first subdir child */
  err = get_next_child_subdir (&child, thisdir, pool);
  if (err)
    return err;

  /* Recurse depth-first: */
  while (child)
    { 
      /* write 3 "down" tags into xml_buffer */
      size_t remember_this_offset = xml_buffer->len;  /* backup */
      svn_string_appendbytes (xml_buffer, " <replace name=\"", 16, pool);
      svn_string_appendstr (xml_buffer, current_dir, pool);
      svn_string_appendbytes (xml_buffer,
                              "\"> <dir> <tree-delta> ", 22, pool);

      err = do_crawl (child, xml_buffer, xml_parser, pool);
      
      if (err)
        {
          if (err->apr_err == SVN_ERR_UNFRUITFUL_DESCENT)
            /* remove 3 "down" tags from xml_buffer */
            xml_buffer->len = remember_this_offset;  /* restore */

          else
            return err;  /* uh-oh, a _real_ error */
        }
      
      else  /* err->apr_err == SVN_NO_ERROR */
        fruitful = 1;
      
      /* Get the next subdir child */
      err = get_next_child_subdir (&child, thisdir, pool);
      if (err)
        return err;
  } 
    
  if (fruitful) 
    {
      /* write 3 "up" tags into xml_buffer */
      svn_string_appendbytes 
        (xml_buffer, " </tree-delta> </dir> </replace> ", 33, pool);
      
      return SVN_NO_ERROR;
    }

  else
    return svn_create_error
      (SVN_ERR_UNFRUITFUL_DESCENT, 0, NULL, pool,
       "kff todo: Ben, what string do you want here?");
}


/*------------------------------------------------------------------*/
/* Public interface.

   Do a depth-first crawl of the local changes in a working copy,
   beginning at ROOT_DIRECTORY (absolute path).  Communicate local
   changes to the supplied EDIT_FNS object.

   (Presumably, the client library will someday grab EDIT_FNS from
   libsvn_ra, and then pass it to this routine.  This is how local
   changes in the working copy are ultimately translated into network
   requests.)  */

svn_error_t *
svn_wc_crawl_local_mods (svn_string_t *root_directory,
                         svn_delta_edit_fns_t *edit_fns,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  svn_delta_xml_parser_t *xml_parser;
  svn_string_t *xml_buffer;

  /* kff todo: I've fooled around with the arguments below to get rid
     of compilation warnings, but that doesn't mean that the call is
     correct.  Take it away, Ben... :-) */
  err = svn_delta_make_xml_parser (&xml_parser,
                                   edit_fns,
                                   NULL,
                                   0,
                                   NULL,
                                   pool);
  if (err)
    return (err);

  xml_buffer = svn_string_create ("", pool);


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
