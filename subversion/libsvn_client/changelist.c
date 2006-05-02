/*
 * changelist.c:  implementation of the 'changelist' command
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "svn_wc.h"


/*** Code. ***/

svn_error_t *
svn_client_changelist(const char *path,
                      const char *changelist_name,
                      svn_boolean_t clear,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  SVN_ERR(svn_wc_tweak_changelist(path, changelist_name, clear, pool));

  /* ### TODO(sussman): create new notification type, and send
         notification feedback.  See locking-commands.c. */
  if (! clear)
    printf("Path '%s' is now part of changelist '%s'.\n",
           path, changelist_name);
  else
    printf("Path '%s' is no longer associated with a changelist'.\n", path);

  return SVN_NO_ERROR;
}


