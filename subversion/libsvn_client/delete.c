/*
 * delete.c:  wrappers around wc delete functionality.
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
svn_client_delete (svn_client_commit_info_t **commit_info,
                   svn_stringbuf_t *path,
                   svn_boolean_t force, 
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *log_msg,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_string_t str;

  str.data = path->data;
  str.len = path->len;
  if (svn_path_is_url (&str))
    {
      /* This is a remote removal.  */
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_stringbuf_t *anchor, *target;
      const svn_delta_editor_t *editor;
      void *edit_baton;
      void *root_baton;
      svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
      const char *committed_date = NULL;
      const char *committed_author = NULL;
      
      svn_path_split (path, &anchor, &target, pool);

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor->data, pool));

      /* Open an RA session for the URL. Note that we don't have a local
         directory, nor a place to put temp files or store the auth data. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor, NULL,
                                            NULL, FALSE, FALSE, TRUE,
                                            auth_baton, pool));

      /* Ensure a non-NULL log message. */
      if (! log_msg)
        log_msg = svn_stringbuf_create ("", pool);

      /* Fetch RA commit editor */
      SVN_ERR (ra_lib->get_commit_editor (session, &editor, &edit_baton,
                                          &committed_rev,
                                          &committed_date,
                                          &committed_author,
                                          log_msg));

      /* Drive the editor to delete the TARGET. */
      SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, pool,
                                  &root_baton));
      SVN_ERR (editor->delete_entry (target->data, SVN_INVALID_REVNUM, 
                                     root_baton, pool));
      SVN_ERR (editor->close_directory (root_baton));
      SVN_ERR (editor->close_edit (edit_baton));

      /* Fill in the commit_info structure. */
      *commit_info = svn_client__make_commit_info (committed_rev,
                                                   committed_author,
                                                   committed_date,
                                                   pool);
      
      /* Free the RA session */
      SVN_ERR (ra_lib->close (session));

      return SVN_NO_ERROR;
    }
  
  /* Mark the entry for deletion. */
  SVN_ERR (svn_wc_delete (path, notify_func, notify_baton, pool));

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
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
