/*
 * propset-cmd.c -- Display status information in current directory
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propset (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  const char *pname, *pname_utf8;
  svn_string_t *propval = NULL;
  svn_boolean_t propval_came_from_cmdline;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR (svn_opt_parse_num_args (&args, os,
                                   opt_state->filedata ? 1 : 2, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata)
    {
      propval = svn_string_create_from_buf (opt_state->filedata, pool);
      propval_came_from_cmdline = FALSE;
    }
  else
    {
      propval = svn_string_create (((const char **) (args->elts))[1], pool);
      propval_came_from_cmdline = TRUE;
    }
  
  /* We only want special Subversion property values to be in UTF-8
     and LF line endings.  All other propvals are taken literally. */
  if (svn_prop_needs_translation (pname_utf8))
    SVN_ERR (svn_subst_translate_string (&propval, propval,
                                         opt_state->encoding, pool));
  else 
    if (opt_state->encoding)
      return svn_error_create 
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         "Bad encoding option: prop's value isn't stored as UTF8.");
  
  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL, *target;
      svn_client_auth_baton_t *auth_baton;

      /* All property commands insist on a specific revision when
         operating on a revprop. */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        return svn_cl__revprop_no_rev_error (pool);

      /* Else some revision was specified, so proceed. */

      /* Implicit "." is okay for revision properties; it just helps
         us find the right repository. */
      svn_opt_push_implicit_dot_target (targets, pool);

      auth_baton = svn_cl__make_auth_baton (opt_state, pool);

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      SVN_ERR (svn_cl__get_url_from_target (&URL, target, pool));  
      if (URL == NULL)
        return svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                "Either a URL or versioned item is required.");

      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_set (pname_utf8, propval,
                                       URL, &(opt_state->start_revision),
                                       auth_baton, &rev, pool));
      if (! opt_state->quiet) 
        {
          const char *target_native;
          SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                              target, pool));
          printf ("property `%s' set on repository revision '%"
                  SVN_REVNUM_T_FMT"'\n",
                  pname, rev);
        }      
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         "Cannot specify revision for setting versioned property '%s'.",
         pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
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
                 "explicit target required ('%s' interpreted as prop value)",
                 propval->data);
            }
          else
            {
              return svn_error_create
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 "explicit target argument required.\n");
            }
        }

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          SVN_ERR (svn_client_propset (pname_utf8, propval, target,
                                       opt_state->recursive, pool));
          
          if (! opt_state->quiet) 
            {
              const char *target_native;
              SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                                  target, pool));
              printf ("property `%s' set%s on '%s'\n",
                      pname, 
                      opt_state->recursive ? " (recursively)" : "",
                      target_native);
            }
        }
    }

  return SVN_NO_ERROR;
}
