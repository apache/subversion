/*
 * rollback-cmd.c -- Rolling back a specific commit in a working copy.
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
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/


svn_error_t *
svn_cl__rollback (apr_getopt_t *os,
                  svn_cl__opt_state_t *opt_state,
                  apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;
  const svn_delta_editor_t *trace_editor;
  void *trace_edit_baton;
  const char *parent_dir, *entry;
  const char *sourcepath1, *sourcepath2, *targetpath;
  svn_error_t *err;
  int i;

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (opt_state->end_revision.kind != svn_client_revision_unspecified
      || opt_state->start_revision.kind == svn_client_revision_unspecified)
    {
      return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0, pool, 
                               "One and only one revision required.");
    }

  opt_state->end_revision.value.number 
    = opt_state->start_revision.value.number - 1;

  if (! SVN_IS_VALID_REVNUM (opt_state->end_revision.value.number))
    {
      return svn_error_createf
        (SVN_ERR_CLIENT_BAD_REVISION, 0, 0, pool,
         "cannot rollback revision %" SVN_REVNUM_T_FMT ".",
         opt_state->start_revision.value.number);
    }

  opt_state->end_revision.kind = svn_client_revision_number;

  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);
 
  svn_cl__push_implicit_dot_target (targets, pool);

  for (i = 0; i < targets->nelts; ++i)
    {
      targetpath = sourcepath1 = sourcepath2 
        = ((const char **) (targets->elts))[i];

      SVN_ERR (svn_wc_get_actual_target (targetpath, &parent_dir, &entry, 
                                         pool));

      SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor, 
                                                &trace_edit_baton,
                                                parent_dir, FALSE, TRUE, pool));

      err = svn_client_merge (trace_editor, trace_edit_baton,
                              auth_baton,
                              sourcepath1,
                              &(opt_state->start_revision),
                              sourcepath2,
                              &(opt_state->end_revision),
                              targetpath,
                              opt_state->nonrecursive ? FALSE : TRUE,
                              opt_state->force,
                              pool); 
      if (err)
         return svn_cl__may_need_force (err);

    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
