/*
 * propdel-cmd.c -- Remove property from files/dirs
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "cl.h"

/* We shouldn't be including a private header here, but it is
 * necessary for fixing issue #3416 */
#include "private/svn_opt_private.h"

#include "svn_private_config.h"


/*** Code. ***/

struct notify_wrapper_baton
{
  void *real_baton;
  svn_wc_notify_func2_t real_func;
  svn_boolean_t found_deleted_nonexistent;
};

/* This checks for deleted_nonexistent before calling the notification function.
   This implements `svn_wc_notify_func2_t'. */
static void
notify_wrapper(void *baton, const svn_wc_notify_t *n, apr_pool_t *pool)
{
  struct notify_wrapper_baton *nwb = baton;

  nwb->found_deleted_nonexistent |=
                (n->action == svn_wc_notify_property_deleted_nonexistent);
  nwb->real_func(nwb->real_baton, n, pool);
}

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
  struct notify_wrapper_baton nwb = { 0 };
  int i;

  /* Get the property's name (and a UTF-8 version of that name). */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname_utf8, pname, pool));
  /* No need to check svn_prop_name_is_valid for *deleting*
     properties, and it may even be useful to allow, in case invalid
     properties sneaked through somehow. */

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));


  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (! opt_state->quiet)
    {
      svn_cl__get_notifier(&nwb.real_func, &nwb.real_baton, FALSE,
                           FALSE, FALSE, pool);
      ctx->notify_func2 = notify_wrapper;
      ctx->notify_baton2 = &nwb;
    }

  SVN_ERR(svn_opt__eat_peg_revisions(&targets, targets, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_set2(pname_utf8, NULL, NULL,
                                      URL, &(opt_state->start_revision),
                                      &rev, FALSE, ctx, pool));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
               _("Cannot specify revision for deleting versioned property '%s'"),
               pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* For each target, remove the property PNAME. */
      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          svn_commit_info_t *commit_info;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          /* Pass FALSE for 'skip_checks' because it doesn't matter here,
             and opt_state->force doesn't apply to this command anyway. */
          SVN_ERR(svn_cl__try(svn_client_propset3
                              (&commit_info, pname_utf8,
                               NULL, target,
                               opt_state->depth,
                               FALSE, SVN_INVALID_REVNUM,
                               opt_state->changelists, NULL,
                               ctx, subpool),
                              NULL, opt_state->quiet,
                              SVN_ERR_UNVERSIONED_RESOURCE,
                              SVN_ERR_ENTRY_NOT_FOUND,
                              SVN_NO_ERROR));
          if (nwb.found_deleted_nonexistent)
            return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Attempting to delete nonexistent property '%s'"),
                             pname);
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
