/*
 * unlcok-cmd.c -- Unlock a working copy path.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_cmdline.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/


struct lock_baton
{
  svn_boolean_t had_print_error;
  apr_pool_t *pool;
};

/* This callback is called by the client layer with BATON, and the
 * PATH being locked.  The LOCK itself should be NULL (We're just
 * conforming to the svn_lock_callback_t prototype).  DO_LOCK should
 * always be false since we're unlocking files here.
 */
static svn_error_t *
print_unlock_info (void *baton,
                   const char *path,
                   svn_boolean_t do_lock,
                   const svn_lock_t *lock,
                   svn_error_t *ra_err)
{
  svn_error_t *err = NULL;
  struct lock_baton *lb = baton;

  if (ra_err)
    svn_handle_error (ra_err, stderr, FALSE);
  else
    err = svn_cmdline_printf (lb->pool, _("Unlocked '%s'.\n"), path);

  if (err)
    {
      /* Print if it is the first error. */
      if (!lb->had_print_error)
        {
          lb->had_print_error = TRUE;
          svn_handle_error (err, stderr, FALSE);
        }
      svn_error_clear (err);
    }

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unlock (apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  struct lock_baton lb;


  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os,
                                          opt_state->targets, pool));

  /* We don't support unlock on directories, so "." is not relevant. */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  lb.had_print_error = FALSE;
  lb.pool = pool;
  SVN_ERR (svn_client_unlock (targets, opt_state->force, 
                              print_unlock_info, &lb, ctx, pool));

  return SVN_NO_ERROR;
}
