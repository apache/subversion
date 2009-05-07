/*
 * propset-cmd.c -- Set property values on files/dirs
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
#include "svn_string.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_props.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propset(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname, *pname_utf8;
  svn_string_t *propval = NULL;
  svn_boolean_t propval_came_from_cmdline;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR(svn_opt_parse_num_args(&args, os,
                                 opt_state->filedata ? 1 : 2, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname_utf8, pname, pool));
  if (! svn_prop_name_is_valid(pname_utf8))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             pname_utf8);

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata)
    {
      propval = svn_string_create_from_buf(opt_state->filedata, pool);
      propval_came_from_cmdline = FALSE;
    }
  else
    {
      propval = svn_string_create(APR_ARRAY_IDX(args, 1, const char *), pool);
      propval_came_from_cmdline = TRUE;
    }

  /* We only want special Subversion property values to be in UTF-8
     and LF line endings.  All other propvals are taken literally. */
  if (svn_prop_needs_translation(pname_utf8))
    SVN_ERR(svn_subst_translate_string(&propval, propval,
                                       opt_state->encoding, pool));
  else
    if (opt_state->encoding)
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("--encoding option applies only to textual"
           " Subversion-controlled properties"));

  /* Suck up all the remaining arguments into a targets array */

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  /* Implicit "." is okay for revision properties; it just helps
     us find the right repository. */
  if (opt_state->revprop)
    svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_set2(pname_utf8, propval, NULL,
                                      URL, &(opt_state->start_revision),
                                      &rev, opt_state->force, ctx, pool));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot specify revision for setting versioned property '%s'"),
         pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* The customary implicit dot rule has been prone to user error
       * here.  People would do intuitive things like
       *
       *    $ svn propset svn:executable script
       *
       * and then be surprised to get an error like:
       *
       *    svn: Illegal target for the requested operation
       *    svn: Cannot set svn:executable on a directory ()
       *
       * So we don't do the implicit dot thing anymore.  A * target
       * must always be explicitly provided when setting a versioned
       * property.  See
       *
       *    http://subversion.tigris.org/issues/show_bug.cgi?id=924
       *
       * for more details.
       */

      if (targets->nelts == 0)
        {
          if (propval_came_from_cmdline)
            {
              return svn_error_createf
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target required ('%s' interpreted as prop value)"),
                 propval->data);
            }
          else
            {
              return svn_error_create
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target argument required"));
            }
        }

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          svn_commit_info_t *commit_info;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));
          SVN_ERR(svn_cl__try(svn_client_propset3
                              (&commit_info, pname_utf8, propval, target,
                               opt_state->depth, opt_state->force,
                               SVN_INVALID_REVNUM, opt_state->changelists,
                               NULL, ctx, subpool),
                              NULL, opt_state->quiet,
                              SVN_ERR_UNVERSIONED_RESOURCE,
                              SVN_ERR_ENTRY_NOT_FOUND,
                              SVN_NO_ERROR));

          if (! opt_state->quiet)
            svn_cl__check_boolean_prop_val(pname_utf8, propval->data, subpool);
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
