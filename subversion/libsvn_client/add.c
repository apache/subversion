/*
 * add.c:  wrappers around wc add functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_add (svn_string_t *path, 
                svn_boolean_t recursive,
                apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_error_t * err;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  
  if (kind == svn_node_file)
    {
      if (recursive)
        {
          /* todo: a gentle warning about trying to recurse on a file
             might be in order here. */
        }
      else
        err = svn_wc_add_file (path, pool);
    }
  else if (kind == svn_node_dir)
    {
      if (recursive)
        {
          /* todo: um...recursively mark a tree for addition. */
        }
      else
        err = svn_wc_add_directory (path, pool);
    }
  else
    return
      svn_error_createf (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
                         "No such object: %s", path->data);

  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_ENTRY_EXISTS)
        return svn_error_quick_wrap 
          (err,
           "svn warning: Cannot add because entry already exists.");
      else
          return err;
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_unadd (svn_string_t *path, 
                  apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_unadd (path, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
