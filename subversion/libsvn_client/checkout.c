/*
 * checkout.c:  wrappers around wc checkout functionality
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

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Public Interfaces. ***/

svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_root_dir_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_root_dir_baton,
                     svn_string_t *path,
                     svn_string_t *xml_src,
                     svn_string_t *ancestor_path,
                     svn_revnum_t ancestor_revision,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal (before_editor,
                                        before_root_dir_baton,
                                        after_editor,
                                        after_root_dir_baton,
                                        path,
                                        xml_src,
                                        ancestor_path,
                                        ancestor_revision,
                                        pool);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
