/*
 * delete.c:  wrappers around wc delete functionality.
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

#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_delete (svn_string_t *path, svn_boolean_t force, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;

  /* kff todo: hmm, this seems to be file-specific, which is probably bad. */

  /* Mark the entry for deletion. */
  err = svn_wc_delete (path, pool);
  if (err)
    return err;

  if (force)
    {
      /* Remove the file. */
      apr_err = apr_file_remove (path->data, pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "svn_client_delete: error deleting %s",
                                  path->data);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
