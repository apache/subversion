/*
 * revert-cmd.c -- Subversion revert command
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_string.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "cl.h"



/*** Code. ***/

svn_error_t *
svn_cl__revert (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  int i;
  svn_boolean_t recursive = opt_state->recursive;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;
  apr_pool_t *subpool;

  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Revert has no implicit dot-target `.', so don't you put that code here! */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, FALSE, FALSE, pool); 

  subpool = svn_pool_create (pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      
      target = svn_path_canonicalize_nts (target, pool);
      SVN_ERR (svn_client_revert (target, recursive, 
                                  notify_func, notify_baton, subpool));
      svn_pool_clear (subpool);
    }
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
