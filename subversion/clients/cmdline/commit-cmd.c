/*
 * commit-cmd.c -- Check changes into the repository.
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

#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "cl.h"

#include "client_errors.h"



svn_error_t *
svn_cl__commit (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_stringbuf_t *base_dir;
  svn_client_auth_baton_t *auth_baton;
  svn_client_commit_info_t *commit_info = NULL;
  svn_revnum_t revnum;
    
  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments. */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* Condense the targets (like commit does)... */
  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      svn_stringbuf_t *parent_dir, *basename;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, 
                                         &basename, pool));
      if (basename)
        svn_stringbuf_set (base_dir, parent_dir->data);
    }

  /* Get revnum set to something meaningful, to cover the xml case. */
  if (opt_state->start_revision.kind == svn_client_revision_number)
    revnum = opt_state->start_revision.value.number;
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, this is fine */

  /* Commit. */
  SVN_ERR (svn_client_commit 
           (&commit_info,
            NULL, NULL,
            NULL, NULL,
            SVN_CL_NOTIFY(opt_state), 
            svn_cl__make_notify_baton (pool),
            auth_baton,
            targets,
            &svn_cl__get_log_message,
            svn_cl__make_log_msg_baton (opt_state, base_dir, pool),
            opt_state->xml_file,
            revnum,
            opt_state->nonrecursive,
            pool));
  if (commit_info)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
