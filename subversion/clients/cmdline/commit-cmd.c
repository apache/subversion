/*
 * commit-cmd.c -- Check changes into the repository.
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
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__commit (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_stringbuf_t *message;
  svn_stringbuf_t *base_dir;
  svn_stringbuf_t *cur_dir;
  svn_stringbuf_t *remainder;
  svn_stringbuf_t *trace_dir;
  const svn_delta_edit_fns_t *trace_editor;
  void *trace_edit_baton;
  svn_client_auth_baton_t *auth_baton;

  /* Take our message from ARGV or a FILE */
  if (opt_state->filedata) 
    message = opt_state->filedata;
  else
    message = opt_state->message;
  
  targets = svn_cl__args_to_target_array (os, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* Get the current working directory as an absolute path. */
  SVN_ERR (svn_path_get_absolute (&cur_dir,
                                  svn_stringbuf_create (".", pool),
                                  pool));

  /* Condense the targets (like commit does)... */
  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      svn_path_local_style,
                                      pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      svn_stringbuf_t *parent_dir, *basename;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, 
                                         &basename, pool));
      if (basename)
        svn_stringbuf_set (base_dir, parent_dir->data);
    }

  /* ...so we can have a common parent path to pass to the trace
     editor.  Note that what we are actually passing here is the
     difference between the absolute path of the current working
     directory and the absolute path of the common parent directory
     used in the commit (if there is a concise difference). */
  remainder = svn_path_is_child (cur_dir, base_dir,
                                 svn_path_local_style, pool);
  if (remainder)
    trace_dir = remainder;
  else
    trace_dir = base_dir;

  SVN_ERR (svn_cl__get_trace_commit_editor 
           (&trace_editor,
            &trace_edit_baton,
            trace_dir,
            pool));

  /* Commit. */
  SVN_ERR (svn_client_commit (NULL, NULL,
                              opt_state->quiet ? NULL : trace_editor, 
                              opt_state->quiet ? NULL : trace_edit_baton,
                              auth_baton,
                              targets,
                              message,
                              opt_state->xml_file,
                              opt_state->revision,
                              pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
