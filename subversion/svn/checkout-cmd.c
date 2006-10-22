/*
 * checkout-cmd.c -- Subversion checkout command
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "cl.h"

#include "svn_private_config.h"


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
svn_cl__checkout(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  const char *local_dir;
  const char *repos_url;
  int i;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* Add a path if the user only specified URLs */
  local_dir = ((const char **) (targets->elts))[targets->nelts - 1];
  if (svn_path_is_url(local_dir))
    {
      if (targets->nelts == 1)
        {
          svn_opt_revision_t pegrev;

          /* Discard the peg-revision, if one was provided. */
          SVN_ERR(svn_opt_parse_path(&pegrev, &local_dir, local_dir, pool));
          if (pegrev.kind != svn_opt_revision_unspecified)
            local_dir = svn_path_canonicalize(local_dir, pool);

          local_dir = svn_path_basename(local_dir, pool);
          local_dir = svn_path_uri_decode(local_dir, pool);
        }
      else
        {
          local_dir = "";
        }
      (*((const char **) apr_array_push(targets))) = local_dir;
    }
  else
    {
      /* What?  They gave us one target, and it wasn't a URL. */
      if (targets->nelts == 1)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, TRUE, FALSE,
                         FALSE, pool);

  subpool = svn_pool_create(pool);
  for (i = 0; i < targets->nelts - 1; ++i)
    {
      const char *target_dir;
      const char *true_url;
      svn_opt_revision_t revision = opt_state->start_revision;
      svn_opt_revision_t peg_revision;

      svn_pool_clear(subpool);

      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Validate the REPOS_URL */
      repos_url = ((const char **) (targets->elts))[i];
      if (! svn_path_is_url(repos_url))
        return svn_error_createf 
          (SVN_ERR_BAD_URL, NULL, 
           _("'%s' does not appear to be a URL"), repos_url);

      /* Get a possible peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &true_url, repos_url,
                                 subpool));

      true_url = svn_path_canonicalize(true_url, subpool);

      /* Use sub-directory of destination if checking-out multiple URLs */
      if (targets->nelts == 2)
        {
          target_dir = local_dir;
        }
      else
        {
          target_dir = svn_path_basename(true_url, subpool);
          target_dir = svn_path_uri_decode(target_dir, subpool);
          target_dir = svn_path_join(local_dir, target_dir, subpool);
        }

      /* Checkout doesn't accept an unspecified revision, so default to
         the peg revision, or to HEAD if there wasn't a peg. */
      if (revision.kind == svn_opt_revision_unspecified)
      {
        if (peg_revision.kind != svn_opt_revision_unspecified)
          revision = peg_revision;
        else
          revision.kind = svn_opt_revision_head;
      }

      SVN_ERR(svn_client_checkout2(NULL, true_url, target_dir,
                                   &peg_revision,
                                   &revision,
                                   opt_state->nonrecursive ? FALSE : TRUE,
                                   opt_state->ignore_externals,
                                   ctx, subpool));
    }
  svn_pool_destroy(subpool);
  
  return SVN_NO_ERROR;
}
