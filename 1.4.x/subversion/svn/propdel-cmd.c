/*
 * propdel-cmd.c -- Remove property from files/dirs
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
#include "svn_utf.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propdel(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* Get the property's name (and a UTF-8 version of that name). */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR(svn_utf_cstring_to_utf8(&pname_utf8, pname, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_set(pname_utf8, NULL,
                                     URL, &(opt_state->start_revision),
                                     &rev, FALSE, ctx, pool));
      if (! opt_state->quiet) 
        {
          SVN_ERR(svn_cmdline_printf(pool,
                                     _("property '%s' deleted from"
                                       " repository revision %ld\n"),
                                     pname_utf8, rev));
        }      
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot specify revision for deleting versioned property '%s'"),
         pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      /* For each target, remove the property PNAME. */
      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          svn_boolean_t success;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          /* Pass FALSE for 'skip_checks' because it doesn't matter here,
             and opt_state->force doesn't apply to this command anyway. */
          SVN_ERR(svn_cl__try(svn_client_propset2(pname_utf8, NULL, target,
                                                  opt_state->recursive,
                                                  FALSE, ctx, subpool),
                              &success, opt_state->quiet,
                              SVN_ERR_UNVERSIONED_RESOURCE,
                              SVN_ERR_ENTRY_NOT_FOUND,
                              SVN_NO_ERROR));
          
          if (success && (! opt_state->quiet))
            {
              SVN_ERR(svn_cmdline_printf
                      (subpool, opt_state->recursive
                       ? _("property '%s' deleted (recursively) from '%s'.\n")
                       : _("property '%s' deleted from '%s'.\n"),
                       pname_utf8, svn_path_local_style(target, subpool)));
            }
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
