/*
 * proplist-cmd.c -- List properties of files/dirs
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__proplist (apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;

  /* Suck up all remaining args in the target array. */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  if (opt_state->revprop)  /* operate on revprops */
    {
      svn_revnum_t rev;
      const char *URL, *target;
      apr_hash_t *proplist;

      /* All property commands insist on a specific revision when
         operating on revprops. */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        return svn_cl__revprop_no_rev_error (pool);

      /* Else some revision was specified, so proceed. */

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      SVN_ERR (svn_client_url_from_path (&URL, target, pool));
      if (URL == NULL)
        return svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                "Either a URL or versioned item is required.");
  
      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_list (&proplist, 
                                        URL, &(opt_state->start_revision),
                                        &rev, ctx, pool));
      
      printf("Unversioned properties on revision %"SVN_REVNUM_T_FMT":\n",
             rev);

      SVN_ERR (svn_cl__print_prop_hash
               (proplist, (! opt_state->verbose), pool));
    }
  else  /* operate on normal, versioned properties (not revprops) */
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          apr_array_header_t *props;
          int j;
          svn_error_t *err;

          svn_pool_clear (subpool);
          err = svn_client_proplist (&props, target,
                                     &(opt_state->start_revision),
                                     opt_state->recursive, ctx, subpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
                {
                  if (!opt_state->quiet)
                    {
                      svn_handle_warning (stderr, err);
                    }
                  continue;
                }
              else
                  return err;
            }

          for (j = 0; j < props->nelts; ++j)
            {
              svn_client_proplist_item_t *item 
                = ((svn_client_proplist_item_t **)props->elts)[j];
              const char *node_name_stdout;
              SVN_ERR (svn_cmdline_cstring_from_utf8
                       (&node_name_stdout, item->node_name->data, subpool));
              printf("Properties on '%s':\n", node_name_stdout);
              SVN_ERR (svn_cl__print_prop_hash
                       (item->prop_hash, (! opt_state->verbose), subpool));
            }
          SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
        }
      svn_pool_destroy (subpool);
    }

  return SVN_NO_ERROR;
}
