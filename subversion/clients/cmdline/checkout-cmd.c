/*
 * checkout-cmd.c -- Subversion checkout command
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "cl.h"


/*** Code. ***/

/* 
  This is what it does

  - case 1: one URL
    $ svn co http://host/repos/module
    checkout into ./module/
  
  - case 2: one URL and explicit path
    $ svn co http://host/repos/module path
    checkout into ./path/
  
  - case 3: multiple URLs
    $ svn co http://host1/repos1/module1 http://host2/repos2/module2
    checkout into ./module1/ and ./module2/
  
  - case 4: multiple URLs and explicit path
    $ svn co http://host1/repos1/module1 http://host2/repos2/module2 path
    checkout into ./path/module1/ and ./path/module2/

  Is this the same as CVS?  Does it matter if it is not?
*/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__checkout (apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_pool_t *subpool;
  svn_client_auth_baton_t *auth_baton;
  apr_array_header_t *targets;
  const char *local_dir;
  const char *repos_url;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;
  int i;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* If there are no targets at all, then let's just give the user a
     friendly help message, rather than silently exiting.  */
  if (targets->nelts < 1)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                             "" /* message is unused */);

  /* Add a path if the user only specified URLs */
  local_dir = ((const char **) (targets->elts))[targets->nelts-1];
  if (svn_path_is_url (local_dir))
    {
      if (targets->nelts == 1)
        local_dir = svn_path_basename (((const char **) (targets->elts))[0],
                                       pool);
      else
        local_dir = "";
      (*((const char **) apr_array_push (targets))) = local_dir;
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, TRUE, FALSE, pool); 

  subpool = svn_pool_create (pool);
  for (i = 0; i < targets->nelts - 1; ++i)
    {
      const char *target_dir;

      /* Validate the REPOS_URL */
      repos_url = ((const char **) (targets->elts))[i];
      if (! svn_path_is_url (repos_url))
        return svn_error_createf 
          (SVN_ERR_BAD_URL, NULL, 
           "`%s' does not appear to be a URL", repos_url);

      repos_url = svn_path_canonicalize (repos_url, subpool);

      /* Use sub-directory of destination if checking-out multiple URLs */
      if (targets->nelts == 2)
        target_dir = local_dir;
      else
        target_dir = svn_path_join (local_dir,
                                    svn_path_basename (repos_url, subpool),
                                    subpool);

      /* ### BUG?  Need a new auth_baton each time, allocating once from
         ### pool doesn't work. Even when allocated from pool the
         ### auth_baton appears to allocate username and password from the
         ### subpool, so when the subpool is cleared nasty things
         ### happen. */
      auth_baton = svn_cl__make_auth_baton (opt_state, subpool);

      SVN_ERR (svn_client_checkout (notify_func,
                                    notify_baton,
                                    auth_baton,
                                    repos_url,
                                    target_dir,
                                    &(opt_state->start_revision),
                                    opt_state->nonrecursive ? FALSE : TRUE,
                                    subpool));
      svn_pool_clear (subpool);
    }
  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}
