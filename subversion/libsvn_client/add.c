/*
 * add.c:  wrappers around wc add/mkdir functionality.
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

#include <string.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

static svn_error_t *
add_dir_recursive (const char *dirname,
                   svn_wc_notify_func_t notify_added,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  apr_status_t apr_err;
  apr_pool_t *subpool;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  /* Add this directory to revision control. */
  SVN_ERR (svn_wc_add (svn_stringbuf_create (dirname, pool),
                       NULL, SVN_INVALID_REVNUM,
                       notify_added, notify_baton, pool));

  /* Create a subpool for iterative memory control. */
  subpool = svn_pool_create (pool);

  /* Read the directory entries one by one and add those things to
     revision control. */
  apr_err = apr_dir_open (&dir, dirname, pool);
  for (apr_err = apr_dir_read (&this_entry, flags, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       apr_err = apr_dir_read (&this_entry, flags, dir))
    {
      svn_stringbuf_t *fullpath;

      /* Skip over SVN admin directories. */
      if (strcmp (this_entry.name, SVN_WC_ADM_DIR_NAME) == 0)
        continue;

      /* Skip entries for this dir and its parent.  */
      if ((strcmp (this_entry.name, ".") == 0)
          || (strcmp (this_entry.name, "..") == 0))
        continue;

      /* Construct the full path of the entry. */
      fullpath = svn_stringbuf_create (dirname, subpool);
      svn_path_add_component 
        (fullpath,
         svn_stringbuf_create (this_entry.name, subpool));

      if (this_entry.filetype == APR_DIR)
        /* Recurse. */
        SVN_ERR (add_dir_recursive (fullpath->data,
                                    notify_added, notify_baton,
                                    subpool));

      else if (this_entry.filetype == APR_REG)
        SVN_ERR (svn_wc_add (fullpath, NULL, SVN_INVALID_REVNUM,
                             notify_added, notify_baton, subpool));

      /* Clean out the per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Destroy the per-iteration pool. */
  svn_pool_destroy (subpool);

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (apr_err)))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, "error during recursive add of `%s'",
         dirname);
    }
  else  /* Yes, it exited cleanly, so close the dir. */
    {
      apr_err = apr_dir_close (dir);
      if (! (APR_STATUS_IS_SUCCESS (apr_err)))
        return svn_error_createf
          (apr_err, 0, NULL, subpool, "error closing dir `%s'", dirname);
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_add (svn_stringbuf_t *path, 
                svn_boolean_t recursive,
                svn_wc_notify_func_t notify_func,
                void *notify_baton,
                apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_error_t *err = NULL;

  SVN_ERR (svn_io_check_path (path->data, &kind, pool));
  if ((kind == svn_node_dir) && (recursive))
    err = add_dir_recursive (path->data, notify_func, notify_baton, pool);
  else
    err = svn_wc_add (path, NULL, SVN_INVALID_REVNUM,
                      notify_func, notify_baton, pool);

  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS))
    return svn_error_quick_wrap 
      (err, "svn warning: Cannot add because entry already exists.");

  return err;
}

svn_error_t *
svn_client_mkdir (svn_client_commit_info_t **commit_info,
                  svn_stringbuf_t *path,
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *log_msg,
                  svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  apr_pool_t *pool)
{
  svn_string_t path_str;
  apr_status_t apr_err;

  path_str.data = path->data;
  path_str.len = path->len;

  /* If this is a URL, we want to drive a commit editor to create this
     directory. */
  if (svn_path_is_url (&path_str))
    {
      /* This is a remote directory creation.  */
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_stringbuf_t *anchor, *target;
      const svn_delta_editor_t *editor;
      void *edit_baton;
      void *root_baton, *dir_baton;
      svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
      const char *committed_date = NULL;
      const char *committed_author = NULL;

      svn_path_split (path, &anchor, &target, pool);

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor->data, pool));

      /* Open a repository session to the URL. Note that we do not have a
         base directory, do not want to store auth data, and do not
         (necessarily) have an admin area for temp files. */
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

      /* Drive the editor to create the TARGET. */
      SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, pool,
                                  &root_baton));
      SVN_ERR (editor->add_directory (target->data, root_baton, NULL, 
                                      SVN_INVALID_REVNUM, pool, &dir_baton));
      SVN_ERR (editor->close_directory (dir_baton));
      SVN_ERR (editor->close_directory (root_baton));
      SVN_ERR (editor->close_edit (edit_baton));

      /* Fill in the commit_info structure. */
      *commit_info = svn_client__make_commit_info (committed_rev,
                                                   committed_author,
                                                   committed_date,
                                                   pool);

      /* Free the RA session. */
      SVN_ERR (ra_lib->close (session));

      return SVN_NO_ERROR;
    }

  /* This is a regular "mkdir" + "svn add" */
  apr_err = apr_dir_make (path->data, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool, path->data);
  
  return svn_wc_add (path, NULL, SVN_INVALID_REVNUM,
                     notify_func, notify_baton, pool);
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
