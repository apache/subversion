/*
 * proplist-cmd.c -- List properties of files/dirs
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_xml.h"
#include "cl.h"

#include "svn_private_config.h"

typedef struct
{
  svn_cl__opt_state_t *opt_state;
  svn_boolean_t is_url;
} proplist_baton_t;


/*** Code. ***/

/* This implements the svn_proplist_receiver_t interface, printing XML to
   stdout. */
static svn_error_t *
proplist_receiver_xml(void *baton,
                      const char *path,
                      apr_hash_t *prop_hash,
                      apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((proplist_baton_t *)baton)->opt_state;
  svn_boolean_t is_url = ((proplist_baton_t *)baton)->is_url;
  svn_stringbuf_t *sb = NULL;
  const char *name_local;

  if (! is_url)
    name_local = svn_path_local_style(path, pool);
  else
    name_local = path;

  /* "<target ...>" */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "target",
                        "path", name_local, NULL);

  SVN_ERR(svn_cl__print_xml_prop_hash(&sb, prop_hash, (! opt_state->verbose),
                                      pool));

  /* "</target>" */
  svn_xml_make_close_tag(&sb, pool, "target");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* This implements the svn_proplist_receiver_t interface. */
static svn_error_t *
proplist_receiver(void *baton,
                  const char *path,
                  apr_hash_t *prop_hash,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((proplist_baton_t *)baton)->opt_state;
  svn_boolean_t is_url = ((proplist_baton_t *)baton)->is_url;
  const char *name_local;

  if (! is_url)
    name_local = svn_path_local_style(path, pool);
  else
    name_local = path;

  if (!opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, _("Properties on '%s':\n"), name_local));
  return svn_cl__print_prop_hash(prop_hash, (! opt_state->verbose), pool);
}


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

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Add "." if user passed 0 file arguments */
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

      if (opt_state->xml)
        {
          svn_stringbuf_t *sb = NULL;
          char *revstr = apr_psprintf(pool, "%ld", rev);

          SVN_ERR(svn_cl__xml_print_header("properties", pool));

          svn_xml_make_open_tag(&sb, pool, svn_xml_normal,
                                "revprops",
                                "rev", revstr, NULL);
          SVN_ERR(svn_cl__print_xml_prop_hash
                  (&sb, proplist, (! opt_state->verbose), pool));
          svn_xml_make_close_tag(&sb, pool, "revprops");

          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
          SVN_ERR(svn_cl__xml_print_footer("properties", pool));
        }
      else
        {
          SVN_ERR
            (svn_cmdline_printf(pool,
                                _("Unversioned properties on revision %ld:\n"),
                                rev));

          SVN_ERR(svn_cl__print_prop_hash
                  (proplist, (! opt_state->verbose), pool));
        }
    }
  else  /* operate on normal, versioned properties (not revprops) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      svn_proplist_receiver_t pl_receiver;

      if (opt_state->xml)
        {
          SVN_ERR(svn_cl__xml_print_header("properties", pool));
          pl_receiver = proplist_receiver_xml;
        }
      else
        {
          pl_receiver = proplist_receiver;
        }

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          proplist_baton_t pl_baton;
          const char *truepath;
          svn_opt_revision_t peg_revision;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          pl_baton.is_url = svn_path_is_url(target);
          pl_baton.opt_state = opt_state;

          /* Check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                     subpool));

          SVN_ERR(svn_cl__try
                  (svn_client_proplist3(truepath, &peg_revision,
                                        &(opt_state->start_revision),
                                        opt_state->depth,
                                        opt_state->changelists,
                                        pl_receiver, &pl_baton,
                                        ctx, subpool),
                   NULL, opt_state->quiet,
                   SVN_ERR_UNVERSIONED_RESOURCE,
                   SVN_ERR_ENTRY_NOT_FOUND,
                   SVN_NO_ERROR));
        }

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_footer("properties", pool));

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
