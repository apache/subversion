/*
 * diff-cmd.c -- Display context diff of a file
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/

/* Compare working copy against a given repository version. */
static svn_error_t *
wc_repository_diff (apr_getopt_t *os,
                    svn_cl__opt_state_t *opt_state,
                    apr_pool_t *pool)
{
  apr_array_header_t *options;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_client_auth_baton_t *auth_baton;
  int i;

  if (opt_state->start_revision != opt_state->end_revision)
    {
      return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                                "diff only supports a single revision");
    }

  options = svn_cl__stringlist_to_array(opt_state->extensions, pool);

  targets = svn_cl__args_to_target_array (os, pool);
  svn_cl__push_implicit_dot_target (targets, pool);
  SVN_ERR (svn_path_remove_redundancies (&condensed_targets,
                                         targets,
                                         svn_path_local_style,
                                         pool));

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  for (i = 0; i < condensed_targets->nelts; ++i)
    {
      svn_stringbuf_t *target
        = ((svn_stringbuf_t **) (condensed_targets->elts))[i];
      svn_stringbuf_t *parent_dir, *entry;

      SVN_ERR (svn_wc_get_actual_target (target, &parent_dir, &entry, pool));
      
      SVN_ERR (svn_client_diff (target,
                                options,
                                auth_baton,
                                opt_state->start_revision,
                                opt_state->start_date,
                                opt_state->nonrecursive ? FALSE : TRUE,
                                pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_cl__cmd_proc_t to handle the 'diff' command. */
svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_array_header_t *targets;
  apr_array_header_t *options;
  svn_boolean_t recurse = TRUE;
  int i;

  /* If a revision has been specified then compare against the repository.
     ### TODO: This won't catch 'svn diff -rHEAD:1' since that doesn't
     change the options from their default value. */
  if (opt_state->start_revision != SVN_INVALID_REVNUM
      || opt_state->end_revision != 1)
    {
      return wc_repository_diff (os, opt_state, pool);
    }

  options = svn_cl__stringlist_to_array(opt_state->extensions, pool);
  targets = svn_cl__args_to_target_array(os, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  /* Check whether the user specified no recursion. */
  if (opt_state->nonrecursive)
    {
      recurse = FALSE;
    }

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      enum svn_node_kind kind;

      SVN_ERR (svn_io_check_path (target, &kind, pool));

      switch (kind) 
        {
        case svn_node_file:
          err = svn_cl__print_file_diff (target, options, pool);
          break;
        case svn_node_dir:
          err = svn_cl__print_dir_diff (target, options, recurse, pool);
          break;
        case svn_node_unknown:
          err = svn_error_createf (0, 0, NULL, pool,
                                  "File type unrecognized for target `%s'.",
                                  target->data);
          break;                                  
        case svn_node_none:
          err = svn_error_createf (APR_ENOENT, 0, NULL, pool,
                                   "Target `%s' not found.", target->data);
          break;
        }

      if (err) return err;
    }
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
