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
  svn_string_t *message;
  svn_string_t *base_dir;
  svn_string_t *cur_dir;
  const svn_delta_edit_fns_t *trace_editor;
  void *trace_edit_baton;

  /* Take our message from ARGV or a FILE */
  if (opt_state->filedata) 
    message = opt_state->filedata;
  else
    message = opt_state->message;
  
  targets = svn_cl__args_to_target_array (os, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* Get the current working directory as an absolute path. */
  SVN_ERR (svn_path_get_absolute (&cur_dir,
                                  svn_string_create (".", pool),
                                  pool));

  /* Condense the targets (like commit does)... */
  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      pool));

  /* ...so we can have a common parent path to pass to the trace
     editor.  Note that what we are actually passing here is the
     difference between the absolute path of the current working
     directory and the absolute path of the common parent directory
     used in the commit (give or take a slash :-). */
  SVN_ERR (svn_cl__get_trace_commit_editor 
           (&trace_editor,
            &trace_edit_baton,
            svn_string_create (&(base_dir->data[cur_dir->len + 1]), pool),
            pool));

  /* Commit. */
  SVN_ERR (svn_client_commit (NULL, NULL,
                              trace_editor, trace_edit_baton,
                              targets,
                              message,
                              opt_state->xml_file,
                              opt_state->revision,
                              pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
