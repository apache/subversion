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
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a file under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied file in dst_parent
 */
static svn_error_t *
copy_file_administratively (svn_stringbuf_t *src_path, 
                            svn_stringbuf_t *dst_parent,
                            svn_stringbuf_t *dst_basename,
                            apr_pool_t *pool)
{
  enum svn_node_kind dst_kind;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  svn_stringbuf_t *dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "'%s' already exists and is in the way.",
                              dst_path->data);

  /* Now, make an actual copy of the working file. */
  SVN_ERR (svn_io_copy_file (src_path, dst_path, pool));

  /* Copy the pristine text-base over.  Why?  Because it's the *only*
     way we can detect any upcoming local mods on the copy.

     In other words, we're talking about the scenario where somebody
     makes local mods to 'foo.c', then does an 'svn cp foo.c bar.c'.
     In this case, bar.c should still be locally modified too.
     
     Why do we want the copy to have local mods?  Even though the user
     will only see an 'A' instead of an 'M', local mods means that the
     client doesn't have to send anything but a small delta during
     commit; the server can make efficient use of the copyfrom args. 

     As long as we're copying the text-base over, we should copy the
     working and pristine propfiles over too. */
  {
    enum svn_node_kind kind;
    svn_stringbuf_t *src_wprop, *src_bprop, *dst_wprop, *dst_bprop;

    /* Discover the paths to the two text-base files */
    svn_stringbuf_t *src_txtb = svn_wc__text_base_path (src_path, FALSE, pool);
    svn_stringbuf_t *dst_txtb = svn_wc__text_base_path (dst_path, FALSE, pool);

    /* Discover the paths to the four prop files */
    SVN_ERR (svn_wc__prop_path (&src_wprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&src_bprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_path (&dst_wprop, dst_basename, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&dst_bprop, dst_basename, 0, pool));

    /* Copy the text-base over unconditionally. */
    SVN_ERR (svn_io_copy_file (src_txtb, dst_txtb, pool));

    /* Copy the props over if they exist. */
    SVN_ERR (svn_io_check_path (src_wprop, &kind, pool));
    if (kind == svn_node_file)
      SVN_ERR (svn_io_copy_file (src_wprop, dst_wprop, pool));
      
    /* Copy the base-props over if they exist */
    SVN_ERR (svn_io_check_path (src_bprop, &kind, pool));
    if (kind == svn_node_file)
      SVN_ERR (svn_io_copy_file (src_bprop, dst_bprop, pool));
  }


  /* Schedule the new file for addition in its parent, WITH HISTORY. */
  SVN_ERR (svn_wc_add_file (dst_path, src_path, pool));


  return SVN_NO_ERROR;
}




/* This function effectively creates and schedules a dir for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a dir under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied dir in dst_parent
 */
static svn_error_t *
copy_dir_administratively (svn_stringbuf_t *src_path, 
                           svn_stringbuf_t *dst_parent,
                           svn_stringbuf_t *dst_basename,
                           apr_pool_t *pool)
{
  /* The 'dst_path' is simply dst_parent/dst_basename */
  svn_stringbuf_t *dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);

  /* Recursively copy the whole directory over. 
     
      (This gets us all text-base, props, base-props, as well as entries,
      local mods, schedulings, existences, etc.) */
  SVN_ERR (svn_io_copy_dir_recursively (src_path, dst_parent, dst_basename,
                                        pool));

  /* Schedule the directory for addition in both its parent and itself
     (this_dir) -- WITH HISTORY.  This function should leave the
     existing administrative dir untouched.  */
  SVN_ERR (svn_wc_add_directory (dst_path, src_path, pool));
 
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

  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  
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
