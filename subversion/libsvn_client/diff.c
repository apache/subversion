/*
 * diff.c:  return two temporary file paths that can be diffed
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

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"




/*** Public Interface. ***/

svn_error_t *
svn_client_file_diff (svn_string_t *path,
                      svn_string_t **pristine_copy_path,
                      apr_pool_t *pool)
{
  svn_error_t *err;

  /* Ask the WC layer to make a tmp copy of the pristine text-base and
     return the path to us.  */
  err = svn_wc_get_pristine_copy_path (path, pristine_copy_path, pool);
  
  /* If the WC fails, or doesn't have a text-base, then ask the RA
     layer to deposit a copy somewhere!  */
  if (err)
    /* TODO:  someday when we have RA working, use it here! */
    return err;
  
  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */


