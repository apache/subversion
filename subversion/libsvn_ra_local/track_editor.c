/*
 * track_editor.c : editor implementation which tracks committed targets
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





/* ------------------------------------------------------------------- */

/** exported routine **/


svn_error_t *
svn_ra_local__get_commit_track_editor (svn_delta_edit_fns_t **editor,
                                       void **edit_baton,
                                       apr_pool_t *pool,
                                       svn_ra_local__commit_hook_baton_t
                                                      *hook_baton)
{
  svn_error_t *err;

  svn_delta_edit_fns_t *track_editor = svn_delta_default_editor (pool);
  


  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





