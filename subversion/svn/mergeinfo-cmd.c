/*
 * mergeinfo-cmd.c -- Query merge-relative info.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
#include "svn_cmdline.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_types.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Implements the svn_log_entry_receiver_t interface.  BATON is a
   pointer to a mergeinfo rangelist array. */
static svn_error_t *
print_log_rev(void *baton,
              svn_log_entry_t *log_entry,
              apr_pool_t *pool)
{
  svn_cmdline_printf(pool, "%ld\n", log_entry->revision);
  return SVN_NO_ERROR;
}
 

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__mergeinfo(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *source_url, *target, *truepath;
  svn_opt_revision_t peg_revision;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets, 
                                                      pool));

  /* We expect a single source URL followed by a single target --
     nothing more, nothing less. */
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Not enough arguments given"));
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments given"));
  if (targets->nelts == 2)
    {
      source_url = APR_ARRAY_IDX(targets, 0, const char *);      
      target = APR_ARRAY_IDX(targets, 1, const char *);      
    }
  else
    {
      source_url = APR_ARRAY_IDX(targets, 0, const char *);      
      target = "";
    }
  if (! svn_path_is_url(source_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), source_url);
    
  /* Parse the path into a path and peg revision. */
  SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target, pool));

  /* If no peg-rev was attached to a URL target, then assume HEAD. */
  if ((peg_revision.kind == svn_opt_revision_unspecified)
      && svn_path_is_url(target))
    peg_revision.kind = svn_opt_revision_head;

  /* If no peg-rev was attached to a non-URL target, then assume BASE. */
  if ((peg_revision.kind == svn_opt_revision_unspecified)
      && (! svn_path_is_url(target)))
    peg_revision.kind = svn_opt_revision_base;

  if (opt_state->show_revs == svn_cl__show_revs_merged)
    {
      /* Get the already-merged information. */
      SVN_ERR(svn_client_mergeinfo_log_merged(truepath, &peg_revision, 
                                              source_url, print_log_rev,
                                              NULL, FALSE, ctx, pool));
    }
  else if (opt_state->show_revs == svn_cl__show_revs_eligible)
    {
      SVN_ERR(svn_client_mergeinfo_log_available(truepath, &peg_revision,
                                                 source_url, print_log_rev,
                                                 NULL, FALSE, ctx, pool));
    }
  return SVN_NO_ERROR;
}
