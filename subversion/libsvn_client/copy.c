/*
 * copy.c:  wrappers around wc 'copy' functionality.
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
#include "client.h"



/*** Code. ***/



/* Public Interface */

/* The main work of this function is to figure out exactly which
   arguments need to be passed to svn_wc_copy(). */
svn_error_t *
svn_client_copy (svn_stringbuf_t *src_path,
                 svn_stringbuf_t *dst_path,
                 apr_pool_t *pool)
{
  enum svn_node_kind src_kind, dst_kind;
  svn_stringbuf_t *final_parent = NULL, *final_basename = NULL;

  /* Part I:  Parse the types of the arguments */

  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  
  if (src_kind == svn_node_none)  /* (sanity check) */
    return svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
                              "path `%s' does not exist.", src_path->data);

  
  if (dst_kind == svn_node_none)
    {
      /* If dst does not exist, then dst's basename will become a new file
         or dir added to dst's parent (possibly an implicit '.') */

      svn_stringbuf_t *dst_parent, *dst_basename;
      svn_path_split (dst_path, &dst_parent, &dst_basename,
                      svn_path_local_style, pool);
      
      final_parent = dst_parent;
      final_basename = dst_basename;
    }

  else if (dst_kind == svn_node_file)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "file `%s' already exists.", dst_path->data);

  else if (dst_kind == svn_node_dir)
    {
      /* If dst is a dir, then src's basename will become a new file
         or dir with dst itself. */
      
      svn_stringbuf_t *src_parent, *src_basename;
      svn_path_split (src_path, &src_parent, &src_basename,
                      svn_path_local_style, pool);

      final_parent = dst_path;
      final_basename = src_basename;
    }

  /* ### Extremely temporary, duh.  :-)  */
  printf ("I will now create '%s' within directory '%s'\n",
          final_basename->data, final_parent->data);


  /* Part II:  Have the working copy library do the real work. */

  SVN_ERR (svn_wc_copy (src_path, final_parent, final_basename, pool));


  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
