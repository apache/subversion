/*
 * checkout-cmd.c -- Subversion checkout command
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
svn_cl__checkout (svn_cl__opt_state_t *opt_state,
                  apr_array_header_t *targets,
                  apr_pool_t *pool)
{
  const svn_delta_edit_fns_t *trace_editor;
  void *trace_edit_baton;
  svn_error_t *err;

  err = svn_cl__get_trace_update_editor (&trace_editor,
                                         &trace_edit_baton,
                                         opt_state->target,
                                         pool);
  if (err)
    return err;


  err = svn_client_checkout (NULL, NULL,
                             trace_editor, trace_edit_baton,
                             opt_state->ancestor_path,
                             opt_state->target,
                             opt_state->revision,
                             opt_state->xml_file,
                             pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
