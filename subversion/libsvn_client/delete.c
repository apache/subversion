/*
 * delete.c:  wrappers around wc delete functionality.
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

#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_delete (svn_stringbuf_t *path,
                   svn_boolean_t force, 
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *log_msg,
                   apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_string_t str;

  str.data = path->data;
  str.len = path->len;
  if (svn_path_is_url (&str))
    {
      /* This is a remote removal.  */
      void *ra_baton, *session, *cb_baton;
      svn_ra_plugin_t *ra_lib;
      svn_ra_callbacks_t *ra_callbacks;
      svn_stringbuf_t *anchor, *target;
      const svn_delta_edit_fns_t *editor;
      void *edit_baton;
      void *root_baton;

      svn_path_split (path, &anchor, &target, svn_path_url_style, pool);

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor->data, pool));

      /* Get the client callbacks for auth stuffs. */
      SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                             auth_baton, anchor, TRUE,
                                             TRUE, pool));
      SVN_ERR (ra_lib->open (&session, anchor, ra_callbacks, cb_baton, pool));

      /* Fetch RA commit editor */
      SVN_ERR (ra_lib->get_commit_editor
               (session,
                &editor, &edit_baton,
                log_msg ? log_msg : svn_stringbuf_create ("", pool),
                NULL, NULL, NULL, NULL));

      SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM,
                                  &root_baton));
      SVN_ERR (editor->delete_entry (target, root_baton));
      SVN_ERR (editor->close_edit (edit_baton));

      return SVN_NO_ERROR;
    }
  
  /* Mark the entry for deletion. */
  SVN_ERR (svn_wc_delete (path, pool));

  if (force)
    {
      /* Remove the file. */
      apr_err = apr_file_remove (path->data, pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "svn_client_delete: error deleting %s",
                                  path->data);
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
