/*
 * ra_plugin.c : the main RA module for local repository access
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

#include "ra_local.h"



/*----------------------------------------------------------------*/

/** The commit cleanup routine passed as a "hook" to the filesystem
    editor **/

/* (This is a routine of type svn_fs_commit_hook_t) */
static svn_error_t *
cleanup_commit (svn_revnum_t *new_revision,
                void *baton)
{
  /* Recover our hook baton */
  svn_ra_local__commit_hook_baton_t *hook_baton =
    (svn_ra_local__commit_hook_baton_t *) baton;

  /* Call hook_baton->close_func() on each committed target! */
  /* TODO */

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------*/

/** The reporter routines (for updates) **/


static svn_error_t *
set_directory (void *report_baton,
               svn_string_t *dir_path,
               svn_revnum_t revision)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}
  

static svn_error_t *
set_file (void *report_baton,
          svn_string_t *file_path,
          svn_revnum_t revision)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}
  


static svn_error_t *
finish_report (void *report_baton)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}




/*----------------------------------------------------------------*/

/** The RA plugin routines **/


static svn_error_t *
open (void **session_baton,
      svn_string_t *repository_URL,
      apr_pool_t *pool)
{
  svn_error_t *err;
  svn_ra_local__session_baton_t *baton;
  svn_string_t *repos_path, *fs_path;

  /* When we close the session_baton later, we don't necessarily want
     to kill the main caller's pool; so let's subpool and work from
     there. */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Allocate the session_baton itself in this subpool */ 
  baton = apr_pcalloc (subpool, sizeof(*baton));

  /* And let all other session_baton data use the same subpool */
  baton->pool = subpool;
  baton->repository_URL = repository_URL;
  baton->fs = svn_fs_new (subpool);

  /* Look through the URL, figure out which part points to the
     repository, and which part is the path *within* the
     repository. */
  err = svn_ra_local__split_URL (&(baton->repos_path),
                                 &(baton->fs_path),
                                 baton->repository_URL,
                                 subpool);
  if (err) return err;

  /* Open the filesystem at located at environment `repos_path' */
  err = svn_fs_open_berkeley (baton->fs, baton->repos_path->data);
  if (err) return err;

  /* Return the session baton */
  *session_baton = baton;
  return SVN_NO_ERROR;
}



static svn_error_t *
close (void *session_baton)
{
  svn_error_t *err;

  /* Close the repository filesystem */
  err = svn_fs_close_fs (session_baton->fs);
  if (err) return err;

  /* When we free the session's pool, the entire session and
     everything inside it is freed, which is good.  However, the
     original pool passed to open() is NOT freed, which is also good. */
  apr_pool_destroy (session_baton->pool);

  return SVN_NO_ERROR;
}




static svn_error_t *
get_latest_revnum (void *session_baton,
                   svn_revnum_t *latest_revnum)
{
  svn_error_t *err;

  err = svn_fs_youngest_rev (session_baton->fs, latest_revnum);
  if (err) return err;

  return SVN_NO_ERROR;
}




static svn_error_t *
get_commit_editor (void *session_baton,
                   const svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_revnum_t base_revision,
                   svn_string_t *log_msg,
                   svn_ra_close_commit_func_t close_func,
                   svn_ra_set_wc_prop_func_t set_func,
                   void *close_baton)
{
  svn_error_t *err;
  svn_delta_edit_fns_t *commit_editor, *tracking_editor, *composed_editor;
  void *commit_editor_baton, *tracking_editor_baton, *composed_editor_baton;

  /* Construct a Magick commit-hook baton */
  svn_ra_local__commit_hook_baton_t *hook_baton
    = apr_pcalloc (session_baton->pool, sizeof(*hook_baton));

  hook_baton->pool = session_baton->pool;
  hook_baton->close_func = close_func;
  hook_baton->set_func = set_func;
  hook_baton->close_baton = close_baton;
  hook_baton->target_array = apr_pcalloc (hook_baton->pool,
                                          sizeof(*(hook_baton->target_array)));

  /* Get the filesystem commit-editor */     
  err = svn_fs_get_editor (&commit_editor,
                           &commit_editor_baton,
                           session_baton->fs,
                           base_revision,
                           log_msg,
                           cleanup_commit, /* our post-commit hook */
                           hook_baton,
                           session_baton->pool);
  if (err) return err;

  /* Get the commit `tracking' editor, telling it to store committed
     targets in our hook_baton */
  err = svn_ra_local__get_commit_track_editor (&tracking_editor,
                                               &tracking_editor_baton,
                                               session_baton->pool,
                                               hook_baton);
  if (err) return err;

  /* Compose the two editors */
  err = svn_delta_compose_editors (&composed_editor,
                                   &composed_editor_baton,
                                   commit_editor,
                                   commit_editor_baton,
                                   tracking_editor,
                                   tracking_editor_baton,
                                   session_baton->pool);
  if (err) return err;

  /* Give the magic composed-editor thingie back to the client */
  *editor = composed_editor;
  *edit_baton = composed_edit_baton;

  return SVN_NO_ERROR;
}



static svn_error_t *
do_checkout (void *session_baton,
             const svn_delta_edit_fns_t *editor,
             void *edit_baton)
{
  /* TODO:  someday */

  return SVN_NO_ERROR;
}



static svn_error_t *
do_update (void *session_baton,
           const svn_ra_reporter_t **reporter,
           void **report_baton,
           apr_array_header_t *targets,
           const svn_delta_edit_fns_t *update_editor,
           void *update_baton)
{
  /* TODO:  someday */

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------*/

/** The static reporter and ra_plugin objects **/

static const svn_ra_reporter_t ra_local_reporter = 
{
  set_directory,
  set_file,
  finish_report
};


static const svn_ra_plugin_t ra_local_plugin = 
{
  "ra_local",
  "RA module for accessing file:// URLs",
  open,
  close,
  get_latest_revnum,
  get_commit_editor,
  do_checkout,
  do_update
};


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
