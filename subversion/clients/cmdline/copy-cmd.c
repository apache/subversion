/*
 * copy-cmd.c -- Subversion copy command
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
svn_cl__copy (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_stringbuf_t *src_path, *dst_path;
  svn_string_t path_str;
  svn_client_auth_baton_t *auth_baton = NULL;
  const svn_delta_editor_t *trace_editor = NULL;
  void *trace_edit_baton = NULL;
  svn_boolean_t src_is_url, dst_is_url;
  svn_client_commit_info_t *commit_info = NULL;

  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);
  if (targets->nelts != 2)
    {
      svn_cl__subcommand_help ("copy", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  src_path = ((svn_stringbuf_t **) (targets->elts))[0];
  dst_path = ((svn_stringbuf_t **) (targets->elts))[1];

  /* Figure out which type of trace editor to use. */
  path_str.data = src_path->data;
  path_str.len = src_path->len;
  src_is_url = svn_path_is_url (&path_str);
  path_str.data = dst_path->data;
  path_str.len = dst_path->len;
  dst_is_url = svn_path_is_url (&path_str);

  if ((! src_is_url) && (! dst_is_url))
    /* WC->WC : No trace editor needed. */
    ;
  else if ((! src_is_url) && (dst_is_url))
    {
      /* WC->URL : Use commit trace editor. */
      /* ### todo:
         
         We'd like to use the trace commit editor, but we have a
         couple of problems with that:
         
         1) We don't know where the commit editor for this case will
            be anchored with respect to the repository, so we can't
            use the DST_URL.

         2) While we do know where the commit editor will be driven
            from with respect to our working copy, we don't know what
            basenames will be chosen for our committed things.  So a
            copy of dir1/foo.c to http://.../dir2/foo-copy-c would
            display like: "Adding   dir1/foo-copy.c", which could be a
            bogus path. 
      */
      /*
      svn_stringbuf_t *src_parent = svn_stringbuf_dup (src_path, pool);
      svn_path_remove_component (src_parent);
      SVN_ERR (svn_cl__get_trace_commit_editor (&trace_editor,
                                                &trace_edit_baton,
                                                src_parent, pool));
      */
    }
  else if ((src_is_url) && (! dst_is_url))
    {
      /* URL->WC : Use checkout trace editor. */
      SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor,
                                                &trace_edit_baton,
                                                dst_path,
                                                pool));
    }
  else
    /* URL->URL : No trace editor needed. */
    ;

  SVN_ERR (svn_client_copy 
           (&commit_info,
            src_path, &(opt_state->start_revision), dst_path, auth_baton, 
            &svn_cl__get_log_message,
            svn_cl__make_log_msg_baton (opt_state, NULL, pool),
            NULL, NULL,                   /* no before_editor */
            trace_editor, trace_edit_baton, /* one after_editor */
            SVN_CL_NOTIFY(opt_state),
            svn_cl__make_notify_baton (pool),
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
