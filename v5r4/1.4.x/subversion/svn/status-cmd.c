/*
 * status-cmd.c -- Display status information in current directory
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"



/*** Code. ***/

struct status_baton
{
  /* These fields all correspond to the ones in the
     svn_cl__print_status() interface. */
  svn_boolean_t detailed;
  svn_boolean_t show_last_committed;
  svn_boolean_t skip_unrecognized;
  svn_boolean_t repos_locks;
  apr_pool_t *pool;

  svn_boolean_t had_print_error;  /* To avoid printing lots of errors if we get
                                     errors while printing to stdout */
  svn_boolean_t xml_mode;
};


/* Prints XML target element with path attribute TARGET, using POOL for
   temporary allocations. */
static svn_error_t *
print_start_target_xml(const char *target, apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "target",
                        "path", target, NULL);

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* Prints XML header using POOL for temporary allocations. */
static svn_error_t *
print_header_xml(apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

  svn_xml_make_header(&sb, pool);
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "status", NULL);

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* Prints XML footer using POOL for temporary allocations. */
static svn_error_t *
print_footer_xml(apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

  svn_xml_make_close_tag(&sb, pool, "status");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}

/* Finish a target element by optionally printing an against element if
 * REPOS_REV is a valid revision number, and then printing an target end tag.
 * Use POOL for temporary allocations. */
static svn_error_t *
print_finish_target_xml(svn_revnum_t repos_rev,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);

  if (SVN_IS_VALID_REVNUM(repos_rev))
    {
      const char *repos_rev_str;
      repos_rev_str = apr_psprintf(pool, "%ld", repos_rev);
      svn_xml_make_open_tag(&sb, pool, svn_xml_self_closing, "against",
                            "revision", repos_rev_str, NULL);
    }

  svn_xml_make_close_tag(&sb, pool, "target");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* A status callback function for printing STATUS for PATH. */
static void
print_status(void *baton,
             const char *path,
             svn_wc_status2_t *status)
{
  struct status_baton *sb = baton;
  svn_error_t *err;

  if (sb->xml_mode)
    err = svn_cl__print_status_xml(path, status, sb->pool);
  else
    err = svn_cl__print_status(path, status, sb->detailed,
                               sb->show_last_committed,
                               sb->skip_unrecognized,
                               sb->repos_locks,
                               sb->pool);

  if (err)
    {
      /* Print if it is the first error. */
      if (!sb->had_print_error)
        {
          sb->had_print_error = TRUE;
          svn_handle_error2(err, stderr, FALSE, "svn: ");
        }
      svn_error_clear(err);
    }
}

/* Simpler helper to allow use of svn_cl__try. */
static svn_error_t *
do_status(svn_cl__opt_state_t *opt_state,
          const char *target,
          const svn_opt_revision_t *rev,
          void *status_baton,
          svn_client_ctx_t *ctx,
          apr_pool_t *pool)
{
  svn_revnum_t repos_rev = SVN_INVALID_REVNUM;

  if (opt_state->xml)
    SVN_ERR(print_start_target_xml(svn_path_local_style(target, pool), pool));

  SVN_ERR(svn_client_status2(&repos_rev, target, rev,
                             print_status, status_baton,
                             opt_state->nonrecursive ? FALSE : TRUE,
                             opt_state->verbose,
                             opt_state->update,
                             opt_state->no_ignore,
                             opt_state->ignore_externals,
                             ctx, pool));

  if (opt_state->xml)
    SVN_ERR(print_finish_target_xml(repos_rev, pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__status(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t * subpool;
  int i;
  svn_opt_revision_t rev;
  struct status_baton sb;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  /* We want our -u statuses to be against HEAD. */
  rev.kind = svn_opt_revision_head;

  /* The notification callback, leave the notifier as NULL in XML mode */
  if (! opt_state->xml)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  subpool = svn_pool_create(pool);

  sb.had_print_error = FALSE;

  if (opt_state->xml)
    {
      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(print_header_xml(pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'incremental' option only valid in XML "
                                  "mode"));
    }

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];

      svn_pool_clear(subpool);

      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Retrieve a hash of status structures with the information
         requested by the user. */

      sb.detailed = (opt_state->verbose || opt_state->update);
      sb.show_last_committed = opt_state->verbose;
      sb.skip_unrecognized = opt_state->quiet;
      sb.repos_locks = opt_state->update;
      sb.xml_mode = opt_state->xml;
      sb.pool = subpool;

      SVN_ERR(svn_cl__try(do_status(opt_state, target, &rev, &sb, ctx,
                                    subpool),
                          NULL, opt_state->quiet,
                          SVN_ERR_WC_NOT_DIRECTORY, /* not versioned */
                          SVN_NO_ERROR));
    }

  svn_pool_destroy(subpool);
  if (opt_state->xml && (! opt_state->incremental))
    SVN_ERR(print_footer_xml(pool));

  return SVN_NO_ERROR;
}
