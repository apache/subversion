/*
 * info-cmd.c -- Display information about a resource
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
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_error_t *
svn_cl__info_print_time (apr_time_t atime,
                         const char *desc,
                         apr_pool_t *pool)
{
  const char *time_utf8;

  time_utf8 = svn_time_to_human_cstring (atime, pool);
  SVN_ERR (svn_cmdline_printf (pool, "%s: %s\n", desc, time_utf8));
  return SVN_NO_ERROR;
}


static svn_error_t *
print_info (const char *target,
            const svn_info_t *info,
            apr_pool_t *pool)
{
  SVN_ERR (svn_cmdline_printf (pool, _("Path: %s\n"),
                               svn_path_local_style (target, pool)));

  /* ### remove this someday:  it's only here for cmdline output
     compatibility with svn 1.1 and older.  */
  if (info->kind != svn_node_dir)
    SVN_ERR (svn_cmdline_printf (pool, _("Name: %s\n"),
                                 svn_path_basename(target, pool)));
 
  if (info->URL) 
    SVN_ERR (svn_cmdline_printf (pool, _("URL: %s\n"), info->URL));
           
  if (info->repos_root_URL) 
    SVN_ERR (svn_cmdline_printf (pool, _("Repository Root: %s\n"),
                                 info->repos_root_URL));
 
  if (info->repos_UUID) 
    SVN_ERR (svn_cmdline_printf (pool, _("Repository UUID: %s\n"),
                                 info->repos_UUID));
 
  if (SVN_IS_VALID_REVNUM (info->rev))
    SVN_ERR (svn_cmdline_printf (pool, _("Revision: %ld\n"), info->rev));

  switch (info->kind) 
    {
    case svn_node_file:
      SVN_ERR (svn_cmdline_printf (pool, _("Node Kind: file\n")));
      break;
          
    case svn_node_dir:
      SVN_ERR (svn_cmdline_printf (pool, _("Node Kind: directory\n")));
      break;
          
    case svn_node_none:
      SVN_ERR (svn_cmdline_printf (pool, _("Node Kind: none\n")));
      break;
          
    case svn_node_unknown:
    default:
      SVN_ERR (svn_cmdline_printf (pool, _("Node Kind: unknown\n")));
      break;
    }

  if (info->has_wc_info)
    {
      switch (info->schedule) 
        {
        case svn_wc_schedule_normal:
          SVN_ERR (svn_cmdline_printf (pool, _("Schedule: normal\n")));
          break;
          
        case svn_wc_schedule_add:
          SVN_ERR (svn_cmdline_printf (pool, _("Schedule: add\n")));
          break;
          
        case svn_wc_schedule_delete:
          SVN_ERR (svn_cmdline_printf (pool, _("Schedule: delete\n")));
          break;
          
        case svn_wc_schedule_replace:
          SVN_ERR (svn_cmdline_printf (pool, _("Schedule: replace\n")));
          break;
          
        default:
          break;
        }
      
      if (info->copyfrom_url) 
        SVN_ERR (svn_cmdline_printf (pool, _("Copied From URL: %s\n"),
                                     info->copyfrom_url));
      
      if (SVN_IS_VALID_REVNUM (info->copyfrom_rev))
        SVN_ERR (svn_cmdline_printf (pool, _("Copied From Rev: %ld\n"),
                                     info->copyfrom_rev));
    }
      
  if (info->last_changed_author) 
    SVN_ERR (svn_cmdline_printf (pool, _("Last Changed Author: %s\n"),
                                 info->last_changed_author));
  
  if (SVN_IS_VALID_REVNUM (info->last_changed_rev))
    SVN_ERR (svn_cmdline_printf (pool, _("Last Changed Rev: %ld\n"),
                                 info->last_changed_rev));
  
  if (info->last_changed_date)
    SVN_ERR (svn_cl__info_print_time (info->last_changed_date, 
                                      _("Last Changed Date"), pool));
  
  if (info->has_wc_info)
    {
      if (info->text_time)
        SVN_ERR (svn_cl__info_print_time (info->text_time, 
                                          _("Text Last Updated"), pool));
      
      if (info->prop_time)
        SVN_ERR (svn_cl__info_print_time (info->prop_time, 
                                          _("Properties Last Updated"), pool));
      
      if (info->checksum) 
        SVN_ERR (svn_cmdline_printf (pool, _("Checksum: %s\n"),
                                     info->checksum));
      
      if (info->conflict_old) 
        SVN_ERR (svn_cmdline_printf (pool,
                                     _("Conflict Previous Base File: %s\n"),
                                     svn_path_local_style (info->conflict_old,
                                                           pool)));
 
      if (info->conflict_wrk) 
        SVN_ERR (svn_cmdline_printf
                 (pool, _("Conflict Previous Working File: %s\n"),
                  svn_path_local_style (info->conflict_wrk, pool)));
      
      if (info->conflict_new) 
        SVN_ERR (svn_cmdline_printf (pool,
                                     _("Conflict Current Base File: %s\n"),
                                     svn_path_local_style (info->conflict_new,
                                                           pool)));
 
      if (info->prejfile) 
        SVN_ERR (svn_cmdline_printf (pool, _("Conflict Properties File: %s\n"),
                                     svn_path_local_style (info->prejfile,
                                                           pool)));
    }      

  /* Print extra newline separator. */
  SVN_ERR (svn_cmdline_printf (pool, "\n"));

  return SVN_NO_ERROR;
}



/* A callback of type svn_info_receiver_t. */
static svn_error_t *
info_receiver (void *baton,
               const char *path,
               const svn_info_t *info,
               apr_pool_t *pool)
{
  return print_info (path, info, pool);
}



/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__info (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *subpool = svn_pool_create (pool);
  int i;
  svn_error_t *err;
  svn_opt_revision_t peg_revision;

  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os, 
                                          opt_state->targets, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target (targets, pool);
  
  for (i = 0; i < targets->nelts; i++)
    {
      const char *truepath;
      const char *target = ((const char **) (targets->elts))[i];
      
      svn_pool_clear (subpool);
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR (svn_opt_parse_path (&peg_revision, &truepath, target, subpool));

      /* If no peg-rev was attached to a URL target, then assume HEAD. */
      if ((svn_path_is_url (target))
          && (peg_revision.kind == svn_opt_revision_unspecified))
        peg_revision.kind = svn_opt_revision_head;

      err = svn_client_info (truepath,
                             &peg_revision, &(opt_state->start_revision),
                             info_receiver, NULL,
                             opt_state->recursive, ctx, subpool);

      /* If one of the targets is a non-existent URL or wc-entry,
         don't bail out.  Just warn and move on to the next target. */
      if (err && err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE)
        {
          svn_error_clear (err);
          SVN_ERR (svn_cmdline_printf
                   (subpool, _("%s:  (Not a versioned resource)\n\n"),
                    svn_path_local_style (target, pool)));
          continue;
        }
      else if (err && err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
        {
          svn_error_clear (err);
          SVN_ERR (svn_cmdline_printf
                   (subpool, _("%s:  (Not a valid URL)\n\n"),
                    svn_path_local_style (target, pool)));
          continue;
        }
      else
        return err;

    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
