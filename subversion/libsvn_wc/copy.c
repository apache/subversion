/*
 * copy.c:  wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "wc.h"



/*** Code. ***/


/* This function effectively creates and schedules a file for
   addition, but does extra administrative things to allow it to
   function as a 'copy'. */
static svn_error_t *
copy_file_administratively (svn_stringbuf_t *src_path, 
                            svn_stringbuf_t *dst_parent,
                            svn_stringbuf_t *dst_basename,
                            apr_pool_t *pool)
{
  apr_file_t *orig_tb, *new_tb;

  svn_stringbuf_t *dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);
  
  /* Make an actual copy of the file. */
  SVN_ERR (svn_io_copy_file (src_path, dst_path, pool));

  /* Schedule the file for addition; this should create an new entry
     in the parent. */
  SVN_ERR (svn_wc_add_file (dst_path, pool));

  /* Copy the pristine file over.  Why?  Because it's the only way we
     can detect any upcoming local mods on the copy. 

     Do the copy in a crash-proof way; open one text-base for reading,
     and the "new" text-base for writing.  Suck data from one to
     another, then close both.  The closure will cause a 'move sync'
     on the new copy.  (This method avoids ever having to get specific
     paths.) */
  SVN_ERR (svn_wc__open_text_base (&orig_tb, src_path, APR_READ, pool));
  SVN_ERR (svn_wc__open_text_base (&new_tb, dst_path, APR_WRITE, pool));

  /* ### write data loop here */

  SVN_ERR (svn_wc__close_text_base (orig_tb, src_path, TRUE, pool));
  SVN_ERR (svn_wc__close_text_base (new_tb, dst_path, TRUE, pool));


  /* Copy the working and pristine properties over. */

  /* Tweak the new entry;  it needs 'copyfrom' args now.  */

  /* ### is it necessary to copy the timestamps over?? */


  return SVN_NO_ERROR;
}





static svn_error_t *
copy_dir_administratively (svn_stringbuf_t *src_path, 
                           svn_stringbuf_t *dst_parent,
                           svn_stringbuf_t *dst_basename,
                           apr_pool_t *pool)
{
  /* ### write */
  printf ("Can't copy directories yet.\n");

  return SVN_NO_ERROR;
}






/* Public Interface */

svn_error_t *
svn_wc_copy (svn_stringbuf_t *src_path,
             svn_stringbuf_t *dst_parent,
             svn_stringbuf_t *dst_basename,
             apr_pool_t *pool)
{
  enum svn_node_kind src_kind;

  /* ### stat the src_path, then: */
  
  if (src_kind == svn_node_file)
    SVN_ERR (copy_file_administratively (src_path, dst_parent,
                                         dst_basename, pool));
  
  else if (src_kind == svn_node_dir)
    SVN_ERR (copy_dir_administratively (src_path, dst_parent,
                                        dst_basename, pool));


  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
