/*
 * status.c:  return the status of a working copy dirent
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

/* Given PATH to a working copy directory or file, allocate and return
   a STATUSHASH structure containing the stati of all entries.  If
   DESCEND is non-zero, recurse fully, else do only immediate
   children.  (See svn_wc.h:svn_wc_statuses() for more verbiage on
   this). */
svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_string_t *path,
                   svn_boolean_t descend,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *hash = apr_hash_make (pool);

  err = svn_wc_statuses (hash, path, descend, pool);
  if (err) return err;

  /* TODO: each status structure in the hash now has all fields filled
     in *except* the repos_rev field.  Once libsvn_ra works, we'll
     need to query the repository for this information.  For now, the
     field is just SVN_INVALID_REVNUM.  */

  *statushash = hash;
  
  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
