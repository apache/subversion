/*
 * update-cmd.c -- Bring work tree in sync with repository
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
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__update (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  int i;
  svn_client_auth_baton_t *auth_baton;

  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Build an authentication baton to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* Remove redundancies from the target list while preserving order. */
  SVN_ERR (svn_path_remove_redundancies (&condensed_targets,
                                         targets,
                                         pool));

  for (i = 0; i < condensed_targets->nelts; i++)
    {
      svn_stringbuf_t *target = 
        ((svn_stringbuf_t **) (condensed_targets->elts))[i];
      const svn_delta_editor_t *trace_editor;
      void *trace_edit_baton;
      svn_stringbuf_t *parent_dir, *entry;

      SVN_ERR (svn_wc_get_actual_target (target,
                                         &parent_dir,
                                         &entry,
                                         pool));

      SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor,
                                                &trace_edit_baton,
                                                parent_dir, pool));

      SVN_ERR (svn_client_update
               (NULL, NULL,
                trace_editor, trace_edit_baton,
                auth_baton,
                target,
                opt_state->xml_file,
                &(opt_state->start_revision),
                opt_state->nonrecursive ? FALSE : TRUE,
                SVN_CL_NOTIFY(opt_state), 
                svn_cl__make_notify_baton (pool),
                pool));
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
