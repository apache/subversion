/*
 * split_url.c : divide a file:/ URL into repository and path
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"


svn_error_t *
svn_ra_local__split_URL (svn_string_t **repos_path,
                         svn_string_t **fs_path,
                         svn_string_t *URL,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  
  svn_string_t *URL_copy = svn_string_dup (URL, pool);

  /* Yank path components off the end of URL_copy, storing them in an
     array.  */

  /* (The final call to remove_component should nuke the `file:'
     component) */

  /* Start from the beginning of the array, build up a path,
     successively adding new components and trying to
     svn_fs_open_berkeley().  */






  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





