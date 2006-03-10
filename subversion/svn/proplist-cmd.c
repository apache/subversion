/*
 * proplist-cmd.c -- List properties of files/dirs
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__proplist(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;

  /* Suck up all remaining args in the target array. */
  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->revprop)  /* operate on revprops */
    {
      svn_revnum_t rev;
      const char *URL;
      apr_hash_t *proplist;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_list(&proplist, 
                                      URL, &(opt_state->start_revision),
                                      &rev, ctx, pool));
      
      SVN_ERR
        (svn_cmdline_printf(pool,
                            _("Unversioned properties on revision %ld:\n"),
                            rev));

      SVN_ERR(svn_cl__print_prop_hash
              (proplist, (! opt_state->verbose), pool));
    }
  else  /* operate on normal, versioned properties (not revprops) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          apr_array_header_t *props;
          int j;
          svn_boolean_t is_url = svn_path_is_url(target);
          const char *truepath;
          svn_opt_revision_t peg_revision;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          /* Check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                     subpool));
          
          SVN_ERR(svn_cl__try
                  (svn_client_proplist2(&props, truepath, &peg_revision,
                                        &(opt_state->start_revision),
                                        opt_state->recursive,
                                        ctx, subpool),
                   NULL, opt_state->quiet,
                   SVN_ERR_UNVERSIONED_RESOURCE,
                   SVN_ERR_ENTRY_NOT_FOUND,
                   SVN_NO_ERROR));

          for (j = 0; j < props->nelts; ++j)
            {
              svn_client_proplist_item_t *item 
                = ((svn_client_proplist_item_t **)props->elts)[j];
              const char *name_local;

              if (! is_url)
                name_local = svn_path_local_style(item->node_name->data,
                                                  subpool);
              else
                name_local = item->node_name->data;

              SVN_ERR(svn_cmdline_printf(subpool, _("Properties on '%s':\n"),
                                         name_local));
              SVN_ERR(svn_cl__print_prop_hash
                      (item->prop_hash, (! opt_state->verbose), subpool));
            }
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
