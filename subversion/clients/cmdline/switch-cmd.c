/*
 * switch-cmd.c -- Bring work tree in sync with a different URL
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
svn_cl__switch (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_stringbuf_t *target = NULL, *switch_url = NULL;
  svn_string_t str;
  svn_wc_entry_t *entry;
  svn_client_auth_baton_t *auth_baton;
  const svn_delta_editor_t *trace_editor;
  void *trace_edit_baton;
  svn_stringbuf_t *parent_dir, *base_tgt;

  /* This command should discover (or derive) exactly two cmdline
     arguments: a local path to update ("target"), and a new url to
     switch to ("switch_url"). */
  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);
  if (targets->nelts == 0)
    {
      svn_cl__subcommand_help ("switch", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }
  if (targets->nelts == 1)
    {
      switch_url = ((svn_stringbuf_t **) (targets->elts))[0];
      target = svn_stringbuf_create (".", pool);
    }
  else
    {
      target = ((svn_stringbuf_t **) (targets->elts))[0];
      switch_url = ((svn_stringbuf_t **) (targets->elts))[1];
    }

  /* Validate the switch_url */
  str.data = switch_url->data;
  str.len = switch_url->len;
  if (! svn_path_is_url (&str))
    return svn_error_createf 
      (SVN_ERR_BAD_URL, 0, NULL, pool, 
       "`%s' does not appear to be a URL", switch_url->data);

  /* Validate the target */
  SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, 
       "`%s' does not appear to be a working copy path", target->data);
  
  /* Build an authentication baton to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* We want the switch to print the same letters as a regular update. */
  if (entry->kind == svn_node_file)
    SVN_ERR (svn_wc_get_actual_target (target, &parent_dir, &base_tgt, pool));
  else if (entry->kind == svn_node_dir)
    parent_dir = target;

  SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor,
                                            &trace_edit_baton,
                                            parent_dir,
                                            FALSE, /* is checkout */
                                            FALSE,
                                            pool));

  /* Do the 'switch' update. */
  SVN_ERR (svn_client_switch
           (NULL, NULL, 
            trace_editor, trace_edit_baton,
            auth_baton,
            target,
            switch_url,
            &(opt_state->start_revision),
            opt_state->nonrecursive ? FALSE : TRUE,
            SVN_CL_NOTIFY(opt_state), 
            svn_cl__make_notify_baton (pool),
            pool));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
